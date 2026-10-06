#include "../../support.h"
#include "platform/tc002/audio/AudioClip.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/CoreEngine.h"
#include "core/sound/AudioRouter.h"
#include "core/synth/SongParser.h"
#include "persistence/AssetProbe.h"
#include "platform/linux/host/HostStore.h"
#include "platform/tc002/contract/AudioProtocol.h"
#include "platform/tc002/audio/Tc002AudioLink.h"
#include "platform/tc002/audio/Tc002AudioMixer.h"
#include "platform/tc002/audio/Tc002AudioPlayer.h"
#include "platform/tc002/audio/Tc002AudioSink.h"
#include "platform/tc002/audio/Tc002AudioSources.h"
#include "platform/tc002/audio/Tc002AudioStream.h"
#include "platform/tc002/speech/SpeechSource.h"
#include "system/MonotonicClock.h"
#include "test_mp3pcm/vectors.h"

using namespace awtrix;
using namespace awtrix::tc002;
using namespace mp3vectors;

namespace {

auto& passed = awtrix::test::passed();

constexpr auto check = awtrix::test::require;

bool waitFor(const std::function<bool()>& done, int timeoutMs) {
  const int64_t end = monotonicMs() + timeoutMs;
  while (monotonicMs() < end) {
    if (done()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return done();
}

// The helper end of the socket pair: checks everything the runtime sends and plays the device
// by consuming bytes on demand (synchronous tests) or at a fixed speed (threaded tests).
struct FakeHelper {
  int fd = -1;
  std::mutex mutex;
  uint32_t generation = 0;
  uint32_t header = 0;
  uint32_t rate = 0;
  uint8_t channels = 0;
  uint32_t received = 0;
  // Every PCM byte of every generation.
  uint64_t total = 0;
  uint32_t consumed = 0;
  uint32_t completed = 0;
  uint32_t opens = 0;
  uint32_t stops = 0;
  uint32_t drains = 0;
  bool draining = false;
  int volume = -1;
  std::vector<int16_t> pcm;
  std::vector<uint8_t> types;
  std::vector<uint32_t> openRates;

  std::atomic<bool> running{false};
  std::atomic<bool> stalled{false};
  std::atomic<int> speed{20};
  std::thread thread;

  explicit FakeHelper(int helperFd) : fd(helperFd) {}
  ~FakeHelper() {
    stopThread();
    if (fd >= 0) ::close(fd);
  }

  void send(const uint8_t* data, std::size_t size) {
    if (::send(fd, data, size, MSG_DONTWAIT | MSG_NOSIGNAL) == static_cast<ssize_t>(size)) return;
    check(errno == EPIPE || errno == ECONNRESET, "fake helper can answer while the runtime lives");
  }

  void hello() {
    uint8_t m[TC002_AUDIO_MAX_MESSAGE];
    tc002_audio_hello h{};
    h.device_rate = 44100;
    h.device_channels = 1;
    h.min_db = -60;
    h.max_db = -3;
    h.window_bytes = TC002_AUDIO_WINDOW_BYTES;
    h.max_pcm_bytes = TC002_AUDIO_MAX_PCM_BYTES;
    send(m, tc002_audio_encode_hello(m, sizeof m, &h));
  }

  void status(uint8_t state = TC002_AUDIO_PLAYING, uint8_t error = 0) {
    uint8_t m[TC002_AUDIO_MAX_MESSAGE];
    tc002_audio_status s{};
    s.state = state;
    s.error = error;
    s.consumed_bytes = consumed;
    s.queued_bytes = received - consumed;
    s.completed_generation = completed;
    send(m, tc002_audio_encode_status(m, sizeof m, header, &s));
  }

  // What the helper does when a primer never settles: it ends the generation by itself and
  // reports under generation zero, so nothing it queued is ever counted as consumed.
  void primeTimeout() {
    completed = generation;
    consumed = received;
    draining = false;
    header = 0;
    status(TC002_AUDIO_IDLE, TC002_AUDIO_ERROR_PRIME_TIMEOUT);
  }

  void read() {
    uint8_t m[TC002_AUDIO_MAX_MESSAGE + 1];
    for (;;) {
      const ssize_t n = ::recv(fd, m, sizeof m, MSG_DONTWAIT | MSG_TRUNC);
      if (n <= 0) return;
      tc002_audio_frame frame;
      check(static_cast<std::size_t>(n) <= TC002_AUDIO_MAX_MESSAGE && tc002_audio_parse(m, n, &frame),
            "runtime sends valid bounded datagrams");
      types.push_back(frame.type);
      switch (frame.type) {
        case TC002_AUDIO_OPEN: {
          uint32_t r;
          uint8_t c;
          check(tc002_audio_read_open(&frame, &r, &c), "open carries a usable format");
          check(frame.generation != generation, "every open starts a new generation");
          generation = frame.generation;
          header = generation;
          rate = r;
          channels = c;
          received = consumed = 0;
          draining = false;
          pcm.clear();
          openRates.push_back(r);
          ++opens;
          break;
        }
        case TC002_AUDIO_PCM:
          check(frame.generation == generation && !draining, "PCM only for the open generation");
          check(frame.length % (2u * channels) == 0, "PCM holds whole frames");
          received += frame.length;
          total += frame.length;
          check(received - consumed <= TC002_AUDIO_WINDOW_BYTES, "the credit window is respected");
          for (std::size_t i = 0; i < frame.length; i += 2)
            pcm.push_back(static_cast<int16_t>(tc002_audio_get16(frame.payload + i)));
          break;
        case TC002_AUDIO_DRAIN:
          check(frame.generation == generation, "drain names the open generation");
          draining = true;
          ++drains;
          break;
        case TC002_AUDIO_STOP:
          ++stops;
          if (frame.generation == generation) {
            completed = generation;
            draining = false;
            consumed = received;
            header = 0;
            status(TC002_AUDIO_IDLE);
          }
          break;
        case TC002_AUDIO_VOLUME: {
          uint8_t p;
          check(tc002_audio_read_volume(&frame, &p), "volume within 0..100");
          volume = p;
          break;
        }
        default: check(false, "runtime sends only runtime message types");
      }
    }
  }

  void consume(uint32_t bytes) {
    consumed = std::min(received, consumed + bytes);
    if (draining && consumed == received) {
      completed = generation;
      draining = false;
      status(TC002_AUDIO_IDLE);
      return;
    }
    status();
  }

  void startThread() {
    running = true;
    thread = std::thread([this] {
      int64_t last = monotonicMs();
      while (running) {
        pollfd p{fd, POLLIN, 0};
        ::poll(&p, 1, 5);
        if (stalled) continue;
        std::lock_guard<std::mutex> lock(mutex);
        read();
        const int64_t now = monotonicMs();
        const uint32_t perMs = rate ? rate * channels * 2u / 1000u : 0;
        const uint32_t bytes = static_cast<uint32_t>(now - last) * perMs * static_cast<uint32_t>(speed.load());
        last = now;
        if (bytes && (received != consumed || draining)) consume(bytes);
      }
    });
  }

  void stopThread() {
    running = false;
    if (thread.joinable()) thread.join();
  }

  template <typename T>
  T get(T FakeHelper::*member) {
    std::lock_guard<std::mutex> lock(mutex);
    return this->*member;
  }
};

struct Pair {
  int runtime = -1;
  int helper = -1;
  Pair() {
    int fds[2];
    check(::socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, fds) == 0, "socketpair");
    runtime = fds[0];
    helper = fds[1];
  }
};

std::vector<rtttl::Note> notes(const char* text, uint16_t& timeUnit) {
  const rtttl::Parse parsed = rtttl::parse(text);
  check(parsed.ok, "test melody parses");
  timeUnit = parsed.timeUnit;
  return parsed.notes;
}

std::vector<int16_t> drainSource(PcmSource& source) {
  std::vector<int16_t> out;
  for (;;) {
    const int16_t* samples = nullptr;
    std::size_t frames = 0;
    const PcmSource::Read read = source.next(samples, frames);
    if (read != PcmSource::Read::Data) break;
    out.insert(out.end(), samples, samples + frames * source.channels());
  }
  return out;
}

std::string tempDir() {
  char pattern[] = "/tmp/tc002-audio-XXXXXX";
  const char* dir = ::mkdtemp(pattern);
  check(dir != nullptr, "temporary data directory");
  return dir;
}

void writeAsset(const std::string& devicePath, const void* data, std::size_t size) {
  const std::string path = host::hostPath(devicePath);
  std::filesystem::create_directories(std::filesystem::path(path).parent_path());
  std::FILE* f = std::fopen(path.c_str(), "wb");
  check(f && std::fwrite(data, 1, size, f) == size, "asset written");
  std::fclose(f);
}

// A RIFF/WAVE file as a phone would send it; extra is a whole chunk placed before the data.
std::string wavClip(uint32_t rate, uint16_t channels, const std::vector<int16_t>& samples,
                    uint16_t bits = 16, const std::string& extra = std::string()) {
  std::string out;
  auto put = [&](uint32_t value, unsigned size) {
    for (unsigned i = 0; i < size; ++i) out += static_cast<char>((value >> (8 * i)) & 0xFF);
  };
  const uint32_t data = static_cast<uint32_t>(samples.size() * 2);
  out += "RIFF";
  put(4 + 24 + static_cast<uint32_t>(extra.size()) + 8 + data, 4);
  out += "WAVEfmt ";
  put(16, 4);
  put(1, 2);
  put(channels, 2);
  put(rate, 4);
  put(rate * channels * bits / 8, 4);
  put(channels * bits / 8, 2);
  put(bits, 2);
  out += extra;
  out += "data";
  put(data, 4);
  for (int16_t sample : samples) put(static_cast<uint16_t>(sample), 2);
  return out;
}

std::vector<int16_t> ramp(std::size_t count) {
  std::vector<int16_t> out(count);
  for (std::size_t i = 0; i < count; ++i) out[i] = static_cast<int16_t>(static_cast<int>(i) - 800);
  return out;
}

std::shared_ptr<const std::string> shared(const std::string& bytes) {
  return std::make_shared<const std::string>(bytes);
}

void test_protocol_from_cpp() {
  uint8_t m[TC002_AUDIO_MAX_MESSAGE];
  const int16_t samples[4] = {1, -2, 3, -4};
  tc002_audio_frame frame;
  const std::size_t n = tc002_audio_encode_pcm(m, sizeof m, 3, samples, 4);
  check(n == 16 && tc002_audio_parse(m, n, &frame) && frame.type == TC002_AUDIO_PCM, "C header works from C++");
  check(static_cast<int16_t>(tc002_audio_get16(frame.payload + 2)) == -2, "sample round trip");
}

void test_link_credit_and_control() {
  Pair pair;
  FakeHelper helper(pair.helper);
  Tc002AudioLink link(pair.runtime);
  link.poll();
  check(!link.ready() && !link.failed() && link.credit() == 0, "no credit before hello");
  helper.hello();
  link.poll();
  check(link.ready(), "hello makes the link ready");
  const uint32_t generation = link.open(44100, 1);
  check(generation == 1 && link.credit() == TC002_AUDIO_MAX_PCM_BYTES, "open grants one message of credit");
  std::vector<int16_t> block(2048, 7);
  for (int i = 0; i < 4; ++i) check(link.sendPcm(block.data(), block.size()), "window fills in 4 KiB steps");
  check(link.credit() == 0 && !link.sendPcm(block.data(), 2), "a full window refuses PCM");
  helper.read();
  check(helper.received == TC002_AUDIO_WINDOW_BYTES, "helper got exactly the window");
  helper.consume(6000);
  link.poll();
  check(link.credit() == TC002_AUDIO_MAX_PCM_BYTES, "consumption returns credit");
  check(link.sendPcm(block.data(), 2048) && link.credit() == 6000 - 4096, "credit is exact");
  link.stop();
  helper.read();
  check(helper.stops == 1 && helper.types.back() == TC002_AUDIO_STOP, "stop goes out despite an exhausted window");
  check(link.credit() == 0, "no PCM after stop");
  helper.status();
  link.poll();
  check(link.finished(generation), "completed generation observed");
  helper.status(TC002_AUDIO_FAILED, TC002_AUDIO_ERROR_VENDOR);
  link.poll();
  check(link.failed() && link.failure().find("driver") != std::string::npos, "helper failure is reported");
}

void test_link_eof_and_nonblocking() {
  Pair pair;
  Tc002AudioLink link(pair.runtime);
  {
    FakeHelper helper(pair.helper);
    helper.hello();
    link.poll();
    const int64_t started = monotonicMs();
    for (int i = 0; i < 20000; ++i) link.setVolume(static_cast<uint8_t>(i % 101));
    check(monotonicMs() - started < 2000 && link.wantsWrite(), "a peer that never reads cannot block the link");
  }
  link.poll();
  check(link.failed() && link.failure() == "the speaker helper exited", "peer exit fails the link");
  Tc002AudioLink none(-1);
  check(none.failed(), "no descriptor, no speaker");
}

void test_tone_source() {
  uint16_t unit = 0;
  const std::vector<rtttl::Note> melody = notes("t:d=4,o=5,b=240:c,p,e", unit);
  ToneSource source(melody, unit);
  const std::vector<int16_t> pcm = drainSource(source);
  const std::size_t noteFrames = rtttl::noteMs(16, unit) * 44100 / 1000;
  check(pcm.size() == 3 * noteFrames, "one note length per note including the pause");
  check(pcm.front() == 0 && pcm[noteFrames - 1] == 0 && pcm[noteFrames] == 0, "notes fade in and out");
  int peak = 0;
  for (std::size_t i = 0; i < noteFrames; ++i) peak = std::max(peak, std::abs(static_cast<int>(pcm[i])));
  check(peak > ToneSource::kAmplitude * 9 / 10 && peak <= ToneSource::kAmplitude, "tone amplitude as specified");
  for (std::size_t i = noteFrames; i < 2 * noteFrames; ++i) check(pcm[i] == 0, "a pause is silent");
}

void test_player_tone_drain_and_stop() {
  Pair pair;
  FakeHelper helper(pair.helper);
  Tc002AudioLink link(pair.runtime);
  Tc002AudioPlayer player(link);
  uint16_t unit = 0;
  const std::vector<rtttl::Note> melody = notes("t:d=4,o=5,b=120:c,d,e,f,g", unit);
  ToneSource reference(melody, unit);
  const std::vector<int16_t> expected = drainSource(reference);

  int64_t now = 1000;
  player.play(std::unique_ptr<PcmSource>(new ToneSource(melody, unit)), 42, now);
  player.pump(now);
  helper.read();
  check(helper.volume == 42 && helper.opens == 0, "volume first, nothing opened before hello");
  helper.hello();
  for (int i = 0; i < 2000 && player.active(); ++i) {
    now += 10;
    player.pump(now);
    helper.read();
    helper.consume(882 * 2);
  }
  check(!player.active() && player.takeOutcome() == Tc002AudioPlayer::Outcome::Finished, "tone finishes");
  check(helper.opens == 1 && helper.rate == 44100 && helper.channels == 1, "tone opens 44.1 kHz mono");
  check(helper.pcm == expected, "every synthesized sample reaches the helper once, in order");
  check(helper.drains == 1 && helper.types.back() == TC002_AUDIO_DRAIN, "a finished source drains");

  player.play(std::unique_ptr<PcmSource>(new ToneSource(melody, unit)), 42, now);
  player.pump(now);
  helper.read();
  const std::size_t before = helper.pcm.size();
  player.stop();
  player.pump(now);
  helper.read();
  check(helper.stops == 1 && helper.pcm.size() == before && !player.active(), "stop is immediate and final");
  check(player.takeOutcome() == Tc002AudioPlayer::Outcome::None, "a stopped source has no outcome");

  player.play(std::unique_ptr<PcmSource>(new ToneSource(melody, unit)), 42, now);
  for (int i = 0; i < 1000 && player.active(); ++i) {
    now += 10;
    player.pump(now);
    helper.read();
    if (!helper.draining) helper.consume(882 * 2);
  }
  check(player.takeOutcome() == Tc002AudioPlayer::Outcome::Failed, "a drain that never completes fails");
  helper.read();
  check(helper.types.back() == TC002_AUDIO_STOP, "and is stopped");
}

void test_player_generation_ended_by_helper() {
  Pair pair;
  FakeHelper helper(pair.helper);
  Tc002AudioLink link(pair.runtime);
  Tc002AudioPlayer player(link);
  uint16_t unit = 0;
  const std::vector<rtttl::Note> melody = notes("long:d=1,o=5,b=60:c,d,e", unit);
  helper.hello();
  int64_t now = 0;
  player.play(std::unique_ptr<PcmSource>(new ToneSource(melody, unit)), 50, now);
  for (int i = 0; i < 10; ++i) {
    now += 10;
    player.pump(now);
    helper.read();
  }
  check(helper.opens == 1 && helper.received == TC002_AUDIO_WINDOW_BYTES, "the window fills while priming hangs");
  helper.primeTimeout();
  for (int i = 0; i < 100 && player.active(); ++i) {
    now += 10;
    player.pump(now);
    helper.read();
  }
  check(!player.active() && player.takeOutcome() == Tc002AudioPlayer::Outcome::Failed,
        "a melody whose generation the helper ended fails instead of waiting for credit");
}

void test_player_ready_timeout() {
  Pair pair;
  FakeHelper helper(pair.helper);
  Tc002AudioLink link(pair.runtime);
  Tc002AudioPlayer player(link);
  uint16_t unit = 0;
  const std::vector<rtttl::Note> melody = notes("t:d=4,o=5,b=120:c", unit);
  player.play(std::unique_ptr<PcmSource>(new ToneSource(melody, unit)), 50, 0);
  player.pump(Tc002AudioPlayer::kReadyTimeoutMs - 1);
  check(player.active(), "waits for the helper to start");
  player.pump(Tc002AudioPlayer::kReadyTimeoutMs);
  check(!player.active() && player.takeOutcome() == Tc002AudioPlayer::Outcome::Failed, "gives up without hello");
}

void test_player_mp3(const std::string& dataDir) {
  (void)dataDir;
  writeAsset("/MP3/sine.mp3", ksine_stereo_128k_mp3, sizeof ksine_stereo_128k_mp3);
  Mp3FileSource reference(host::hostPath("/MP3/sine.mp3"));
  const std::vector<int16_t> expected = drainSource(reference);
  check(expected.size() > 30000, "reference decode");

  Pair pair;
  FakeHelper helper(pair.helper);
  Tc002AudioLink link(pair.runtime);
  Tc002AudioPlayer player(link);
  helper.hello();
  std::size_t tapped = 0;
  int64_t now = 0;
  player.play(std::unique_ptr<PcmSource>(new Mp3FileSource(host::hostPath("/MP3/sine.mp3"))), 70, now,
              [&](const int16_t*, std::size_t frames, uint8_t channels, uint32_t rate, int64_t lead) {
                check(channels == 2 && rate == 44100 && lead >= 0, "tap sees the decoded format");
                tapped += frames * channels;
              });
  for (int i = 0; i < 5000 && player.active(); ++i) {
    now += 5;
    player.pump(now);
    helper.read();
    helper.consume(4096);
  }
  check(player.takeOutcome() == Tc002AudioPlayer::Outcome::Finished, "MP3 finishes");
  check(helper.rate == 44100 && helper.channels == 2, "MP3 opens with its own format");
  check(helper.pcm == expected && tapped == expected.size(), "decoded MP3 reaches the helper unchanged");

  Mp3FileSource missing(host::hostPath("/MP3/none.mp3"));
  check(!missing.opened(), "missing file does not open");
}

void test_clip_sources() {
  std::string error;
  const std::vector<int16_t> mono = ramp(1600);
  const std::string wav = wavClip(16000, 1, mono);
  check(checkClip(wav, error), "a 16 kHz mono WAV is a clip");
  std::unique_ptr<PcmSource> source = openClip(shared(wav));
  check(source && source->rate() == 16000 && source->channels() == 1, "and plays at its own format");
  check(drainSource(*source) == mono, "every sample of a WAV clip, in order");

  std::vector<int16_t> stereo = ramp(4800);
  const std::string list("LIST\x03\0\0\0abc\0", 12);
  const std::string tagged = wavClip(48000, 2, stereo, 16, list);
  check(checkClip(tagged, error), "a stereo WAV with an odd-sized chunk before the data");
  source = openClip(shared(tagged));
  check(source && source->rate() == 48000 && source->channels() == 2 && drainSource(*source) == stereo,
        "plays past the chunk it does not know");

  error.clear();
  check(!checkClip(wavClip(16000, 1, mono, 24), error) && error == "unsupported WAV", "24-bit WAV refused");
  check(!checkClip(wavClip(8000, 1, mono), error) && error == "unsupported WAV", "8 kHz WAV refused");
  check(!checkClip(wav.substr(0, wav.size() - 2), error) && error == "unsupported WAV",
        "a WAV shorter than its data chunk says is refused");
  check(!openClip(shared(wav.substr(0, 20))), "and cannot be opened");

  writeAsset("/MP3/clip.mp3", ksine_mono_64k_mp3, sizeof ksine_mono_64k_mp3);
  Mp3FileSource reference(host::hostPath("/MP3/clip.mp3"));
  const std::vector<int16_t> decoded = drainSource(reference);
  const std::string mp3(reinterpret_cast<const char*>(ksine_mono_64k_mp3), sizeof ksine_mono_64k_mp3);
  check(checkClip(mp3, error), "an MPEG-1 MP3 is a clip");
  source = openClip(shared(mp3));
  check(source && drainSource(*source) == decoded && source->rate() == 44100,
        "an MP3 clip decodes exactly like the same file");

  // Bigger than the decoder would ever scan through on its own.
  std::string id3("ID3\x04\0\0", 6);
  const std::size_t tagSize = 200 * 1024;
  id3 += static_cast<char>((tagSize >> 21) & 0x7F);
  id3 += static_cast<char>((tagSize >> 14) & 0x7F);
  id3 += static_cast<char>((tagSize >> 7) & 0x7F);
  id3 += static_cast<char>(tagSize & 0x7F);
  id3.append(tagSize, '\0');
  check(checkClip(id3 + mp3, error), "an ID3 tag in front is fine");
  source = openClip(shared(id3 + mp3));
  check(source && drainSource(*source) == decoded, "and is never decoded");

  const std::string lsf(reinterpret_cast<const char*>(ksine_mono_24khz_48k_mp3), sizeof ksine_mono_24khz_48k_mp3);
  check(checkClip(lsf, error), "an MPEG-2 MP3 is a clip");
  source = openClip(shared(lsf));
  check(source && !drainSource(*source).empty() && source->rate() == 24000, "and plays at 24 kHz");

  error.clear();
  check(!checkClip("hello, clock", error) && error == "not WAV or MP3", "text is no clip");
  std::string lone(2048, '\0');
  const char header[] = {'\xFF', '\xFB', '\x90', '\x64'};
  lone.replace(100, 4, header, 4);
  check(!checkClip(lone, error) && error == "not WAV or MP3", "a lone frame header in noise is no MP3");
  check(!checkClip(std::string(), error), "nothing is no clip");
}

struct Rig {
  struct Display : IDisplayService {
    void sendScreen() override {}
  } display;
  struct System : ISystemService {
    void reboot() override {}
    void sleep(uint64_t) override {}
    void factoryReset() override {}
    void resetSettings() override {}
  } system;
  sound::AudioRouter router;
  AssetProbe assets;
  CoreEngine engine{router, display, system};
  Pair pair;
  FakeHelper helper{pair.helper};
  std::unique_ptr<Tc002AudioSink> sink;

  explicit Rig(bool sayHello = true, std::shared_ptr<speech::SpeechVoice> voice = {}) {
    sink.reset(new Tc002AudioSink(engine, pair.runtime, std::move(voice)));
    router.setTone(sink.get());
    router.setPcm(sink.get());
    router.setAssets(&assets);
    router.setVolumes(100, 30, 20, 10);
    if (sayHello) helper.hello();
    helper.startThread();
  }
  ~Rig() {
    sink.reset();
    helper.stopThread();
  }
  void tick() { router.tick(monotonicMs()); }
};

// One entry of a sound object; the router takes it as the parser would hand it on.
sound::Choices one(sound::Kind kind, const std::string& text, bool loop = false) {
  sound::Choices choices;
  choices.count = 1;
  choices.items[0].kind = kind;
  choices.items[0].text = text;
  choices.items[0].loop = loop;
  return choices;
}

sound::Choices file(const std::string& name, bool loop = false) { return one(sound::Kind::File, name, loop); }

void test_voice_wav_and_ownership() {
  std::vector<uint8_t> wav(44 + 3200, 0);
  auto word = [&](std::size_t at, uint32_t n, unsigned size) { for(unsigned i=0;i<size;++i) wav[at+i]=n>>(i*8); };
  std::memcpy(wav.data(), "RIFF", 4); word(4,wav.size()-8,4); std::memcpy(wav.data()+8,"WAVEfmt ",8);
  word(16,16,4); word(20,1,2); word(22,1,2); word(24,16000,4); word(28,32000,4); word(32,2,2); word(34,16,2);
  std::memcpy(wav.data()+36,"data",4); word(40,3200,4);
  for(unsigned i=0;i<1600;++i) word(44+i*2,static_cast<uint16_t>(static_cast<int>(i)-800),2);
  writeAsset("/MP3/voice.wav",wav.data(),wav.size());
  WavFileSource source(host::hostPath("/MP3/voice.wav"));
  check(source.opened() && source.rate()==16000 && source.channels()==1,"Assist WAV format accepted");
  const int16_t* samples=nullptr;std::size_t frames=0;
  check(source.next(samples,frames)==PcmSource::Read::Data && frames==1600 && samples[0]==-800 && samples[1599]==799,
        "WAV signed samples preserved");
  check(source.next(samples,frames)==PcmSource::Read::End,"WAV data length respected");
  word(34,24,2);writeAsset("/MP3/invalid.wav",wav.data(),wav.size());
  check(!WavFileSource(host::hostPath("/MP3/invalid.wav")).opened(),"unsupported WAV format rejected");
  Rig rig;
  check(rig.sink->playLoop("/MP3/sine.mp3"),"competing loop starts");
  check(waitFor([&]{return rig.helper.get(&FakeHelper::received)>0;},2000),"loop reaches speaker");
  rig.sink->setVoiceOwned(true);
  check(!rig.sink->playMp3("/MP3/sine.mp3", sound::Group::Alert) && !rig.sink->playEffect("/MP3/sine.mp3") &&
        !rig.sink->playRtttl("tone:d=4,o=5,b=120:c", sound::Group::Alert),"voice excludes competing sounds");
  check(waitFor([&]{return rig.sink->voiceQuiet();},2000),"capture waits for speaker stop acknowledgement");
  check(rig.sink->playSystemSound(host::hostPath("/MP3/voice.wav"),0),"voice WAV accepted");
  check(!rig.sink->systemFinished(),"playback is pending, not already complete");
  check(waitFor([&]{return rig.sink->systemFinished();},3000),"voice completion follows speaker drain");
  int64_t audible=-1;
  check(rig.sink->systemStart(audible)==Tc002AudioSink::SystemStart::Audible && audible>=0,"voice was audible");
  rig.sink->setVoiceOwned(false);
  check(rig.sink->playMp3("/MP3/sine.mp3", sound::Group::Alert),"normal sound available after voice");
}

// Samples of one value, so the helper can tell which clip it is hearing.
std::string steadyWav(int16_t value, std::size_t frames) {
  return wavClip(16000, 1, std::vector<int16_t>(frames, value));
}

bool heard(FakeHelper& helper, int16_t below) {
  std::lock_guard<std::mutex> lock(helper.mutex);
  return std::any_of(helper.pcm.begin(), helper.pcm.end(), [&](int16_t s) { return s < below; });
}

void test_sink_clips() {
  Rig rig;
  DispatchDetail detail;
  check(rig.router.caps().clip && rig.sink->clips(), "the TC002 speaker plays clips");
  rig.helper.speed = 1;

  std::string longClip = steadyWav(4000, 16000 * 10);
  check(tc002::playClip(rig.sink.get(), rig.router, std::move(longClip), detail) == DispatchResult::Ok && longClip.empty(),
        "a long clip starts, its bytes taken rather than copied");
  check(rig.sink->oneShotPlaying() && rig.router.alertPlaying(), "and counts as playing from its request on");
  check(waitFor([&] { return rig.helper.get(&FakeHelper::received) > 0; }, 2000), "the clip reaches the helper");
  check(rig.helper.get(&FakeHelper::rate) == 44100 && rig.helper.get(&FakeHelper::channels) == 1,
        "resampled into the mixer's 44.1 kHz mono");
  check(waitFor([&] { return rig.helper.get(&FakeHelper::volume) == 10; }, 2000), "at the alert level");

  check(tc002::playClip(rig.sink.get(), rig.router, steadyWav(-4000, 16000), detail) == DispatchResult::Ok,
        "a second clip replaces it");
  check(waitFor([&] { return heard(rig.helper, -1000); }, 2000), "the second clip is heard");
  check(rig.sink->oneShotPlaying() && rig.router.alertPlaying(), "and counts as playing, not the first");
  check(waitFor([&] { return !rig.sink->oneShotPlaying(); }, 3000) &&
            waitFor([&] { return rig.helper.get(&FakeHelper::drains) == 1; }, 3000),
        "and ends long before the first would have: it is gone");
  check(rig.helper.get(&FakeHelper::opens) == 1, "both in one mixer");
  rig.tick();
  check(!rig.router.alertStatus().playing && rig.router.alertStatus().name == "clip",
        "the state names the clip and shows it ended");

  check(tc002::playClip(rig.sink.get(), rig.router, "not audio", detail) == DispatchResult::ValidationError &&
            detail.message == "not WAV or MP3", "bytes that are no clip are refused");

  check(tc002::playClip(rig.sink.get(), rig.router, steadyWav(4000, 16000 * 10), detail) == DispatchResult::Ok,
        "another long clip");
  check(waitFor([&] { return rig.helper.get(&FakeHelper::opens) == 2; }, 2000), "plays");
  const uint32_t stops = rig.helper.get(&FakeHelper::stops);
  rig.router.stop(sound::Stop::Alert);
  check(!rig.sink->oneShotPlaying() && !rig.router.alertPlaying(), "stopping the alerts stops a clip");
  check(waitFor([&] { return rig.helper.get(&FakeHelper::stops) == stops + 1; }, 1000), "at once");

  rig.sink->setVoiceOwned(true);
  detail.clear();
  check(tc002::playClip(rig.sink.get(), rig.router, steadyWav(4000, 1600), detail) == DispatchResult::Unavailable &&
            detail.message == "speaker unavailable", "the voice assistant's speaker takes no clip");
  check(!rig.sink->oneShotPlaying(), "and nothing is asked to play");
  rig.sink->setVoiceOwned(false);
  check(tc002::playClip(rig.sink.get(), rig.router, steadyWav(4000, 1600), detail) == DispatchResult::Ok,
        "clips play again after the voice");
}

// A steady tone at 24 kHz for as long as asked, cancelled or not.
struct SteadyVoice : speech::SpeechVoice {
  std::size_t samples = 2400;
  std::atomic<int> cancels{0};
  uint32_t rate() const override { return 24000; }
  std::unique_ptr<speech::SpeechUtterance> start(const speech::Plan&) override {
    struct Utterance : speech::SpeechUtterance {
      SteadyVoice& voice;
      std::size_t left;
      std::atomic<bool> cancelled{false};
      Utterance(SteadyVoice& v, std::size_t samples) : voice(v), left(samples) {}
      std::size_t render(int16_t* out, std::size_t max) override {
        if (cancelled) return 0;
        const std::size_t count = std::min(max, left);
        std::fill_n(out, count, int16_t{-3000});
        left -= count;
        return count;
      }
      void cancel() override {
        cancelled = true;
        ++voice.cancels;
      }
    };
    return std::unique_ptr<speech::SpeechUtterance>(new Utterance(*this, samples));
  }
};

void test_sink_speech() {
  DispatchDetail detail;
  const auto say = [](const char* text) { return one(sound::Kind::Speech, text); };
  {
    Rig rig;
    check(!rig.sink->speaks() && !rig.router.caps().speech, "without a voice the speaker does not speak");
    check(rig.router.play(say("Hello"), sound::Group::Alert, "", detail) == sound::PlayResult::NoSink &&
              detail.message == "no text-to-speech", "and says so");
  }
  auto voice = std::make_shared<SteadyVoice>();
  Rig rig(true, voice);
  rig.helper.speed = 1;
  check(rig.sink->speaks() && rig.router.caps().speech, "with a voice it does");
  detail.clear();
  check(!rig.router.check(say("... !"), sound::Origin::Play, detail) && detail.field == "speech" &&
            detail.message == "no words to speak",
        "text without words is refused before it plays");
  detail.clear();
  check(rig.router.play(say("... !"), sound::Group::Alert, "", detail) == sound::PlayResult::Invalid &&
            detail.field == "speech" && detail.message == "no words to speak",
        "and when it is played");
  check(!rig.sink->oneShotPlaying(), "a refused request plays nothing");

  check(rig.router.play(say("Hello"), sound::Group::Alert, "", detail) == sound::PlayResult::Ok,
        "speech starts");
  check(rig.sink->oneShotPlaying() && rig.router.alertPlaying(), "and counts as playing from its request on");
  check(waitFor([&] { return heard(rig.helper, -1000); }, 2000), "the voice reaches the speaker");
  check(waitFor([&] { return !rig.router.alertPlaying(); }, 3000), "and ends once heard");
  rig.tick();
  check(rig.router.alertStatus().name == "speech" && !rig.router.alertStatus().playing,
        "the state names speech and shows it ended");

  voice->samples = 24000 * 30;
  const uint32_t before = rig.helper.get(&FakeHelper::received);
  check(rig.router.play(say("Long"), sound::Group::Alert, "", detail) == sound::PlayResult::Ok,
        "a long utterance starts");
  check(waitFor([&] { return rig.helper.get(&FakeHelper::received) > before; }, 2000), "and is heard");
  rig.router.stop(sound::Stop::Alert);
  check(!rig.router.alertPlaying() && waitFor([&] { return voice->cancels == 1; }, 2000),
        "stopping the alerts cancels the voice");

  const auto speaking = [&] {
    const uint32_t from = rig.helper.get(&FakeHelper::received);
    return waitFor([&] { return rig.helper.get(&FakeHelper::received) > from; }, 2000);
  };
  check(rig.router.play(say("Long"), sound::Group::Alert, "", detail) == sound::PlayResult::Ok && speaking(),
        "speech again");
  check(tc002::playClip(rig.sink.get(), rig.router, steadyWav(4000, 1600), detail) == DispatchResult::Ok &&
            waitFor([&] { return voice->cancels == 2; }, 2000),
        "a newer one-shot cancels it");
  check(waitFor([&] { return !rig.router.alertPlaying(); }, 3000), "the clip ends");

  check(rig.router.play(say("Long"), sound::Group::App, "talker", detail) == sound::PlayResult::Ok &&
            rig.router.appSoundPlaying() && speaking(),
        "a script speaks too");
  rig.sink->setVoiceOwned(true);
  check(waitFor([&] { return voice->cancels == 3; }, 2000), "Assist taking the speaker cancels it");
  detail.clear();
  check(rig.router.play(say("Hello"), sound::Group::Alert, "", detail) == sound::PlayResult::NoSink &&
            detail.message == "speaker unavailable", "and no speech starts while Assist has the speaker");
  rig.sink->setVoiceOwned(false);
}

void test_sink_tone_mp3_and_router() {
  Rig rig;
  DispatchDetail detail;
  const sound::Caps caps = rig.router.caps();
  check(caps.rtttl && caps.mp3 && caps.radio && caps.effect && caps.song && caps.url && caps.clip && !caps.track,
        "one sink answers all caps but the track player's");
  check(rig.router.play(one(sound::Kind::Rtttl, "t:d=8,o=5,b=240:c,e,g"), sound::Group::Alert, "", detail) ==
            sound::PlayResult::Ok,
        "RTTTL plays");
  check(rig.sink->isPlaying() && rig.router.alertPlaying(), "playing at once");
  check(waitFor([&] { return !rig.sink->isPlaying(); }, 3000), "the melody ends by itself");
  check(rig.helper.get(&FakeHelper::opens) == 1 && rig.helper.get(&FakeHelper::drains) == 1, "one generation, drained");
  check(rig.helper.get(&FakeHelper::rate) == 44100 && rig.helper.get(&FakeHelper::channels) == 1,
        "the mixer speaks 44.1 kHz mono");
  check(rig.helper.get(&FakeHelper::volume) == 10, "a melody from the API plays at the alert level");

  const uint8_t melody[] = "beep:d=4,o=6,b=200:c";
  writeAsset("/MELODIES/beep.txt", melody, sizeof melody - 1);
  check(rig.sink->playMelodyFile("beep", sound::Group::Alert) && !rig.sink->playMelodyFile("nothere", sound::Group::Alert) &&
            !rig.sink->playMelodyFile("../x", sound::Group::Alert), "melody files by name only");
  check(waitFor([&] { return !rig.sink->isPlaying(); }, 3000), "melody file ends");

  const uint32_t opened = rig.helper.get(&FakeHelper::opens);
  const sound::Choices longMelody = one(sound::Kind::Rtttl, "long:d=1,o=5,b=60:c,d,e,f,g,a,b");
  check(rig.router.play(longMelody, sound::Group::Alert, "", detail) == sound::PlayResult::Ok,
        "a long melody from the API starts");
  check(waitFor([&] { return rig.helper.get(&FakeHelper::opens) > opened &&
                             rig.helper.get(&FakeHelper::received) > 0; }, 2000), "melody reaches the helper");
  check(rig.router.play(file("sine"), sound::Group::App, "player", detail) == sound::PlayResult::Ok,
        "a script's MP3 meanwhile is accepted");
  check(rig.sink->isPlaying() && !rig.sink->oneShotPlaying(), "but never cuts the alert off");

  rig.router.stop(sound::Stop::Alert);
  check(rig.router.play(longMelody, sound::Group::App, "player", detail) == sound::PlayResult::Ok &&
            rig.sink->isPlaying(),
        "once the alert is stopped, a script's long melody plays");
  const uint32_t stops = rig.helper.get(&FakeHelper::stops);
  const uint32_t opens = rig.helper.get(&FakeHelper::opens);
  rig.helper.speed = 1;
  check(rig.router.play(file("sine"), sound::Group::App, "player", detail) == sound::PlayResult::Ok,
        "and the script's MP3 replaces its melody");
  check(rig.sink->oneShotPlaying() && !rig.sink->isPlaying(), "only the MP3 counts as playing");
  check(waitFor([&] { return rig.helper.get(&FakeHelper::volume) == 20; }, 2000),
        "a script's MP3 plays at the app level");
  check(rig.helper.get(&FakeHelper::opens) == opens && rig.helper.get(&FakeHelper::stops) == stops,
        "within the same mixer, without a stop");
  check(rig.router.appStatus().playing && rig.router.appStatus().name == "sine", "state shows the MP3 by name");
  check(waitFor([&] { return !rig.sink->oneShotPlaying(); }, 5000), "MP3 ends by itself");
  check(waitFor([&] { rig.tick(); return !rig.router.appStatus().playing; }, 1000), "state follows");

  check(rig.router.play(file("missing"), sound::Group::Alert, "", detail) == sound::PlayResult::NotFound,
        "unknown MP3");
}

// Turning every level down to 0 silences what plays, and up again it is heard again.
void test_volume_zero_silences_a_playing_mix() {
  Rig rig;
  DispatchDetail detail;
  rig.helper.speed = 1;
  check(rig.router.play(file("fx", true), sound::Group::App, "player", detail) == sound::PlayResult::Ok,
        "a script's music plays");
  check(waitFor([&] { return rig.helper.get(&FakeHelper::volume) == 20; }, 2000), "at the app level");
  rig.router.setVolumes(0, 30, 20, 10);
  check(waitFor([&] { return rig.helper.get(&FakeHelper::volume) == 0; }, 2000), "volume 0 silences it");
  rig.router.setVolumes(50, 30, 20, 10);
  check(waitFor([&] { return rig.helper.get(&FakeHelper::volume) == 10; }, 2000),
        "and it comes back at master times app");
  rig.router.stop(sound::Stop::All);
}

// An alert over a script's music: the speaker plays at the louder level and the mixer scales the
// other down, and an app sound never cuts the alert off.
void test_an_alert_over_app_music() {
  Rig rig;
  DispatchDetail detail;
  rig.helper.speed = 1;
  check(rig.router.play(file("fx", true), sound::Group::App, "player", detail) == sound::PlayResult::Ok,
        "a script's music plays");
  check(waitFor([&] { return rig.helper.get(&FakeHelper::volume) == 20; }, 2000), "at the app level");
  check(tc002::playClip(rig.sink.get(), rig.router, steadyWav(4000, 16000 * 10), detail) == DispatchResult::Ok, "a long alert over it");
  check(rig.router.alertPlaying(), "plays");
  check(rig.helper.get(&FakeHelper::volume) == 20 && rig.helper.get(&FakeHelper::opens) == 1,
        "in the same mixer, the speaker at the louder app level");
  check(rig.router.play(file("sine"), sound::Group::App, "player", detail) == sound::PlayResult::Ok &&
            rig.router.alertPlaying(),
        "a script's sound meanwhile leaves the alert playing");
  rig.router.stop(sound::Stop::All);
}

// Zeros for a fixed number of frames: a one-shot the test can hear the loop duck under.
class Silence final : public PcmSource {
 public:
  explicit Silence(std::size_t frames) : left_(frames), block_(1024, 0) {}
  Read next(const int16_t*& samples, std::size_t& frames) override {
    if (!left_) return Read::End;
    frames = std::min(left_, block_.size());
    left_ -= frames;
    samples = block_.data();
    return Read::Data;
  }
  uint32_t rate() const override { return 44100; }
  uint8_t channels() const override { return 1; }

 private:
  std::size_t left_;
  std::vector<int16_t> block_;
};

void test_mix_buffer_boundaries() {
  class Blocks final : public PcmSource {
   public:
    Blocks(const std::vector<int16_t>& samples, uint32_t rate, std::size_t block, bool& wait)
        : samples_(samples), rate_(rate), block_(block), wait_(wait) {}
    Read next(const int16_t*& samples, std::size_t& frames) override {
      if (at_ && wait_) return Read::Wait;
      if (at_ == samples_.size()) return Read::End;
      frames = std::min(block_, samples_.size() - at_);
      samples = samples_.data() + at_;
      at_ += frames;
      return Read::Data;
    }
    uint32_t rate() const override { return rate_; }
    uint8_t channels() const override { return 1; }
   private:
    const std::vector<int16_t>& samples_;
    uint32_t rate_;
    std::size_t block_, at_ = 0;
    bool& wait_;
  };

  std::vector<int16_t> input(1901);
  for (std::size_t i = 0; i < input.size(); ++i) input[i] = (i * 37) % 4000 - 2000;
  EffectCache cache;
  bool wait = false;
  for (uint32_t rate : {16000u, 44100u, 48000u}) {
    MixSource reference(cache);
    reference.setOneShot(std::unique_ptr<PcmSource>(new Blocks(input, rate, input.size(), wait)));
    const auto expected = drainSource(reference);
    for (std::size_t block : {1u, 2u, 17u, 127u}) {
      MixSource chunked(cache);
      chunked.setOneShot(std::unique_ptr<PcmSource>(new Blocks(input, rate, block, wait)));
      check(drainSource(chunked) == expected, "resampling preserves samples across source block boundaries");
    }
  }

  wait = true;
  MixSource paused(cache);
  paused.setOneShot(std::unique_ptr<PcmSource>(new Blocks(input, 44100, 17, wait)));
  const int16_t* samples = nullptr;
  std::size_t frames = 0;
  check(paused.next(samples, frames) == PcmSource::Read::Data && frames == MixSource::kBlockFrames,
        "a source waiting for data keeps its mixer block");
  check(std::equal(input.begin(), input.begin() + 17, samples) &&
            std::all_of(samples + 17, samples + frames, [](int16_t sample) { return sample == 0; }) &&
            paused.oneShotActive(), "buffered sound plays before a wait without ending the source");
  wait = false;
  const auto resumed = drainSource(paused);
  check(resumed == std::vector<int16_t>(input.begin() + 17, input.end()),
        "a waiting source resumes without repeating or dropping samples");
}

void test_mix_source() {
  writeAsset("/MP3/fx.mp3", ksine_mono_64k_mp3, sizeof ksine_mono_64k_mp3);
  writeAsset("/MP3/fx32.mp3", knoise_stereo_32khz_mp3, sizeof knoise_stereo_32khz_mp3);
  const std::string fx = host::hostPath("/MP3/fx.mp3");
  const std::string fx32 = host::hostPath("/MP3/fx32.mp3");
  Mp3FileSource reference(fx);
  const std::vector<int16_t> ref = drainSource(reference);
  check(reference.channels() == 1 && reference.rate() == 44100 && ref.size() > 4096,
        "effect reference decode");

  EffectCache cache;
  MixSource idle(cache);
  const int16_t* samples = nullptr;
  std::size_t frames = 0;
  check(idle.next(samples, frames) == PcmSource::Read::End, "a mixer with nothing to play ends");
  check(idle.rate() == 44100 && idle.channels() == 1 && idle.leadMs() == MixSource::kLeadMs,
        "the mixer speaks 44.1 kHz mono and caps its lead");

  MixSource shot(cache);
  shot.setOneShot(std::unique_ptr<PcmSource>(new Mp3FileSource(fx)));
  check(shot.oneShotActive() && !shot.takeOneShotStarted(), "a one-shot waits for its first block");
  check(shot.next(samples, frames) == PcmSource::Read::Data && shot.takeOneShotStarted() &&
            !shot.takeOneShotStarted(), "and reports its first block once");
  std::vector<int16_t> alone(samples, samples + frames);
  const std::vector<int16_t> rest = drainSource(shot);
  alone.insert(alone.end(), rest.begin(), rest.end());
  check(alone == ref, "a lone one-shot is its decode, sample for sample, and ends where it ends");
  check(shot.takeOneShotEnd() == MixSource::End::Finished && shot.takeOneShotEnd() == MixSource::End::None,
        "its end is reported once");

  MixSource one(cache);
  check(!one.addEffect(host::hostPath("/MP3/none.mp3")), "a missing effect is refused");
  check(one.addEffect(fx) && one.voices() == 1, "an effect takes a voice");
  const std::vector<int16_t> single = drainSource(one);
  check(single.size() == ref.size(), "an effect plays once");
  check(single == ref, "a lone effect is its decode, sample for sample, as a one-shot is");
  check(cache.find(fx) && cache.bytes() >= ref.size() * sizeof(int16_t),
        "an effect decoded to its end is cached including spare allocation");
  EffectCache bounded;
  auto oversized = std::make_shared<Clip>();
  oversized->samples.reserve(EffectCache::kBudgetBytes / sizeof(int16_t) + 1);
  oversized->samples.push_back(1);
  bounded.put(fx, oversized);
  check(!bounded.find(fx) && bounded.bytes() == 0,
        "spare sample capacity cannot exceed the effect cache budget");

  MixSource again(cache);
  check(again.addEffect(fx) && drainSource(again) == single, "the cached effect sounds the same");

  MixSource two(cache);
  check(two.addEffect(fx) && two.addEffect(fx) && two.voices() == 2, "two effects at once");
  const std::vector<int16_t> both = drainSource(two);
  bool summed = both.size() == single.size();
  for (std::size_t i = 0; summed && i < both.size(); ++i) summed = both[i] == 2 * single[i];
  check(summed, "effects add up");

  MixSource quiet(cache);
  quiet.setGains(MixSource::kUnity, MixSource::kUnity / 2, MixSource::kUnity);
  check(quiet.addEffect(fx), "an effect at half gain");
  const std::vector<int16_t> half = drainSource(quiet);
  bool halved = half.size() == single.size();
  for (std::size_t i = 0; halved && i < half.size(); ++i) halved = std::abs(half[i] - single[i] / 2) <= 1;
  check(halved, "a layer gain scales that layer");

  MixSource crowd(cache);
  for (int i = 0; i < 5; ++i) crowd.addEffect(fx);
  check(crowd.voices() == MixSource::kVoices, "a fifth effect cuts off the oldest");
  crowd.stopEffects();
  check(crowd.voices() == 0 && crowd.idle(), "effects stop together");

  Mp3FileSource reference32(fx32);
  const std::size_t frames32 = drainSource(reference32).size() / 2;
  MixSource resampled(cache);
  check(resampled.addEffect(fx32), "a 32 kHz stereo effect");
  const std::size_t expected = frames32 * 44100 / 32000;
  const std::size_t got = drainSource(resampled).size();
  check(got + 2 >= expected && got <= expected + 2, "is resampled to 44.1 kHz mono");

  MixSource loop(cache);
  check(!loop.setLoop(host::hostPath("/MP3/none.mp3")), "a missing loop is refused");
  check(loop.setLoop(fx) && loop.looping(), "a loop starts");
  bool endless = true;
  for (std::size_t played = 0; endless && played < 4 * ref.size(); played += frames)
    endless = loop.next(samples, frames) == PcmSource::Read::Data && frames == MixSource::kBlockFrames;
  check(endless, "a loop plays past the end of its file");
  check(loop.setLoop(fx) && loop.looping(), "the same loop again keeps playing");

  MixSource plain(cache);
  MixSource under(cache);
  check(plain.setLoop(fx) && under.setLoop(fx), "two mixers with the same loop");
  const int16_t* reference16 = nullptr;
  std::size_t referenceFrames = 0;
  auto step = [&] {
    plain.next(reference16, referenceFrames);
    under.next(samples, frames);
  };
  auto scaledBy = [&](int32_t gain) {
    bool same = frames == referenceFrames;
    for (std::size_t i = 0; same && i < frames; ++i)
      same = std::abs(samples[i] - static_cast<int32_t>((static_cast<int64_t>(reference16[i]) * gain) >> 15)) <= 2;
    return same;
  };
  for (int i = 0; i < 10; ++i) step();
  check(scaledBy(MixSource::kUnity), "in step");
  under.setOneShot(std::unique_ptr<PcmSource>(new Silence(MixSource::kBlockFrames * 20)));
  step();
  bool ducked = true;
  for (int i = 0; i < 19 && ducked; ++i) {
    step();
    ducked = scaledBy(MixSource::kDuckGain);
  }
  check(ducked, "the loop ducks under a one-shot");
  step();
  check(!under.oneShotActive() && under.takeOneShotEnd() == MixSource::End::Finished, "the one-shot ends");
  step();
  step();
  check(scaledBy(MixSource::kUnity), "and the loop comes back");

  // The effect is cached by now, so three mixers play it sample for sample alike.
  MixSource fxAlone(cache), fxUnder(cache), fxDucked(cache);
  check(fxAlone.addEffect(fx) && fxUnder.addEffect(fx) && fxDucked.addEffect(fx), "the same effect three times");
  fxUnder.setOneShot(std::unique_ptr<PcmSource>(new Silence(MixSource::kBlockFrames * 10)));
  fxDucked.setOneShot(std::unique_ptr<PcmSource>(new Silence(MixSource::kBlockFrames * 10)));
  fxDucked.setDuckEffects(true);
  const int16_t* alone16 = nullptr;
  const int16_t* under16 = nullptr;
  const int16_t* ducked16 = nullptr;
  std::size_t aloneFrames = 0, underFrames = 0, duckedFrames = 0;
  bool unducked = true, quieter = true;
  for (int i = 0; i < 8; ++i) {
    fxAlone.next(alone16, aloneFrames);
    fxUnder.next(under16, underFrames);
    fxDucked.next(ducked16, duckedFrames);
    if (i == 0) continue;
    for (std::size_t j = 0; j < aloneFrames; ++j) {
      unducked = unducked && under16[j] == alone16[j];
      quieter = quieter && std::abs(ducked16[j] - ((alone16[j] * MixSource::kDuckGain) >> 15)) <= 2;
    }
  }
  check(unducked, "effects play on unducked under a one-shot by default");
  check(quieter, "and duck like the loop while setDuckEffects is on");

  check(loop.addEffect(fx), "an effect over the loop");
  check(loop.setLoop("") && !loop.looping(), "the loop stops");
  const std::vector<int16_t> tail = drainSource(loop);
  check(!tail.empty() && tail.size() <= ref.size(), "the effect still finishes");

  writeAsset("/MP3/fx.mp3", ksine_stereo_128k_mp3, sizeof ksine_stereo_128k_mp3);
  check(cache.find(fx) == nullptr, "a file replaced on disk decodes again");
  writeAsset("/MP3/fx.mp3", ksine_mono_64k_mp3, sizeof ksine_mono_64k_mp3);
}

// A 1 kHz sine at a fixed amplitude, as loud as a one-shot gets.
class Sine final : public PcmSource {
 public:
  Sine(std::size_t frames, int16_t amplitude) : left_(frames), amplitude_(amplitude), block_(1024) {}
  Read next(const int16_t*& samples, std::size_t& frames) override {
    if (!left_) return Read::End;
    frames = std::min(left_, block_.size());
    for (std::size_t i = 0; i < frames; ++i, ++at_)
      block_[i] = static_cast<int16_t>(std::lround(amplitude_ * std::sin(2 * M_PI * 1000.0 * at_ / 44100.0)));
    left_ -= frames;
    samples = block_.data();
    return Read::Data;
  }
  uint32_t rate() const override { return 44100; }
  uint8_t channels() const override { return 1; }

 private:
  std::size_t left_;
  int16_t amplitude_;
  std::size_t at_ = 0;
  std::vector<int16_t> block_;
};

std::vector<int32_t> sineBlock(double amplitude, std::size_t& at) {
  std::vector<int32_t> block(MixSource::kBlockFrames);
  for (int32_t& v : block) v = static_cast<int32_t>(std::lround(amplitude * std::sin(2 * M_PI * 1000.0 * at++ / 44100.0)));
  return block;
}

int32_t peakOf(const int16_t* samples, std::size_t frames) {
  int32_t peak = 0;
  for (std::size_t i = 0; i < frames; ++i) peak = std::max<int32_t>(peak, std::abs(samples[i]));
  return peak;
}

void test_limiter() {
  audio::Limiter limiter(MixSource::kRate, MixSource::kBlockFrames);
  std::vector<int16_t> out(MixSource::kBlockFrames);
  std::size_t at = 0;
  bool exact = true;
  for (int b = 0; b < 10; ++b) {
    const std::vector<int32_t> quiet = sineBlock(audio::Limiter::kThreshold, at);
    limiter.apply(quiet.data(), out.data(), quiet.size());
    for (std::size_t i = 0; i < quiet.size(); ++i) exact = exact && out[i] == quiet[i];
  }
  check(exact && limiter.gain() == audio::Limiter::kUnity, "a sum up to -1 dBFS passes sample for sample");

  bool caught = true, shaped = true;
  for (int b = 0; b < 20; ++b) {
    const std::vector<int32_t> loud = sineBlock(3.0 * 32767, at);
    limiter.apply(loud.data(), out.data(), loud.size());
    caught = caught && peakOf(out.data(), out.size()) <= audio::Limiter::kThreshold + 1;
    if (!b) continue;
    for (std::size_t i = 0; i < loud.size(); ++i)
      shaped = shaped && std::abs(out[i] - ((static_cast<int64_t>(loud[i]) * limiter.gain()) >> 15)) <= 1;
  }
  check(caught, "three times full scale stays at -1 dBFS from its first sample on");
  check(shaped, "turned down, not clipped: the wave keeps its shape");
  check(std::abs(limiter.gain() - audio::Limiter::kUnity / 3 * audio::Limiter::kThreshold / 32767) < 64,
        "the gain takes the peak to the threshold");

  int32_t last = limiter.gain();
  bool rising = true;
  uint32_t blocks = 0;
  for (; blocks < 200 && limiter.gain() < audio::Limiter::kUnity; ++blocks) {
    const std::vector<int32_t> quiet = sineBlock(8000, at);
    limiter.apply(quiet.data(), out.data(), quiet.size());
    rising = rising && limiter.gain() > last;
    last = limiter.gain();
  }
  check(rising && limiter.gain() == audio::Limiter::kUnity, "after the overload the gain recovers to unity");
  check(blocks > audio::Limiter::kReleaseMs / 10 && blocks < 8 * audio::Limiter::kReleaseMs / 10,
        "over a few release time constants");
  const std::vector<int32_t> after = sineBlock(audio::Limiter::kThreshold, at);
  limiter.apply(after.data(), out.data(), after.size());
  exact = true;
  for (std::size_t i = 0; i < after.size(); ++i) exact = exact && out[i] == after[i];
  check(exact, "and then passes sample for sample again");

  std::vector<int32_t> spike(MixSource::kBlockFrames, 0);
  spike[200] = 4 * 32767;
  spike[201] = -4 * 32767;
  limiter.apply(spike.data(), out.data(), spike.size());
  check(out[200] <= audio::Limiter::kThreshold && out[201] >= -audio::Limiter::kThreshold - 1 && out[0] == 0,
        "a block's peak is turned down where it stands");
  limiter.reset();
  check(limiter.gain() == audio::Limiter::kUnity, "reset starts over at unity");
}

// A sine that holds for a whole bar at 120 bpm, and a noise tick that is over in a few ms.
const char* kHeldSong = "bpm 120\ninst a wave=sine attack=0 sustain=100 gate=100\na: a4:16";
const char* kTickFx = "inst n wave=noise attack=0 decay=5 sustain=0\nn: %x";

std::shared_ptr<const synth::Song> songOf(const char* text) {
  const synth::ParseResult parsed = synth::parse(text);
  check(parsed.ok(), text);
  return parsed.song;
}

int32_t blockPeak(MixSource& mix) {
  const int16_t* samples = nullptr;
  std::size_t frames = 0;
  if (mix.next(samples, frames) != PcmSource::Read::Data) return -1;
  int32_t peak = 0;
  for (std::size_t i = 0; i < frames; ++i) peak = std::max<int32_t>(peak, std::abs(samples[i]));
  return peak;
}

void test_mix_source_synth() {
  EffectCache cache;
  MixSource mix(cache);
  mix.setSong(songOf(kHeldSong), false);
  check(mix.looping() && !mix.idle() && mix.voices() == 0, "a song plays on the loop's layer");
  MixSource::SongPosition at;
  check(!mix.songPosition(at), "no position before the first block");
  const int32_t level = blockPeak(mix);
  check(level > 3000 && level < 32767, "the song sounds at the loop's level");
  check(mix.songPosition(at) && at.beat == 0.0 && std::abs(at.beatsPerMs - 0.002) < 1e-12 &&
            at.endBeat == 4.0 && at.loopBeat == 0.0 && at.loops,
        "the first block starts on the song's top");
  blockPeak(mix);
  check(mix.songPosition(at) && std::abs(at.beat - 441.0 / 22050.0) < 1e-9,
        "each block moves the song on by its length");

  mix.setOneShot(std::unique_ptr<PcmSource>(new Silence(44100)));
  blockPeak(mix);
  const int32_t ducked = blockPeak(mix);
  check(ducked > 0 && ducked * 3 < level, "the song ducks under a one-shot");
  mix.setOneShot(nullptr);
  blockPeak(mix);
  check(blockPeak(mix) * 10 > level * 9, "and comes back after it");

  mix.setLoop("");
  bool ended = false;
  for (int i = 0; i < 10 && !ended; ++i) ended = blockPeak(mix) < 0;
  check(ended && mix.idle() && !mix.songPosition(at), "stopping the loop fades the song out and ends");

  MixSource effects(cache);
  effects.addFx(songOf(kTickFx));
  check(effects.voices() == 1 && !effects.looping(), "a synth effect takes an effect's place");
  check(blockPeak(effects) > 0, "and sounds at once");
  bool over = false;
  for (int i = 0; i < 20 && !over; ++i) over = blockPeak(effects) < 0;
  check(over && effects.voices() == 0, "it plays once and the mixer ends with it");
  for (int i = 0; i < 5; ++i) effects.addFx(songOf(kTickFx));
  check(effects.voices() == MixSource::kVoices, "a fifth synth effect cuts off the oldest");
  effects.stopEffects();
  check(effects.voices() == 0 && effects.idle(), "stopping the effects ends them all");

  std::unique_ptr<PcmSource> once = MixSource::songOnce(songOf(kTickFx));
  check(once->rate() == MixSource::kRate && once->channels() == 1, "a song once is mono at the mixer's rate");
  const std::vector<int16_t> rendered = drainSource(*once);
  check(!rendered.empty() && rendered.size() < MixSource::kRate, "and plays once, then ends");
}

void test_mix_source_overlap() {
  const std::string fx = host::hostPath("/MP3/fx.mp3");
  EffectCache cache;
  MixSource mix(cache);
  mix.setOneShot(std::unique_ptr<PcmSource>(new Sine(MixSource::kRate / 5, 31000)));
  for (std::size_t i = 0; i < MixSource::kVoices; ++i) check(mix.addEffect(fx), "an effect over a loud one-shot");
  mix.setSong(songOf("bpm 120\ninst a wave=saw attack=0 sustain=100 gate=100 volume=200\na: [c4 e g]:16"), false);
  for (std::size_t i = 0; i < MixSource::kVoices; ++i)
    mix.addFx(songOf("inst a wave=pulse attack=0 sustain=100 gate=100 volume=200\na: c5:4"));
  const int16_t* samples = nullptr;
  std::size_t frames = 0;
  std::size_t total = 0, full = 0, over = 0;
  for (int b = 0; b < 30 && mix.next(samples, frames) == PcmSource::Read::Data; ++b) {
    for (std::size_t i = 0; i < frames; ++i) {
      const int32_t magnitude = std::abs(static_cast<int32_t>(samples[i]));
      if (magnitude >= 32767) ++full;
      if (magnitude > audio::Limiter::kThreshold + 1) ++over;
    }
    total += frames;
  }
  check(total == 30 * MixSource::kBlockFrames, "everything at once plays");
  check(full == 0 && over == 0, "and stays at -1 dBFS, where the sum would clip");
}

void test_sink_songs() {
  Rig rig;
  DispatchDetail detail;
  check(rig.sink->synthesizes() && rig.router.caps().song, "the TC002 speaker synthesizes");
  std::string error;
  check(!rig.sink->checkSong("inst a\na: c4 x", error) && error == "unexpected 'x' (line 2, column 7)",
        "a bad song is refused with where it went wrong");
  check(rig.router.play(one(sound::Kind::Song, "inst a\na: c4 x", true), sound::Group::App, "", detail) ==
                sound::PlayResult::Invalid &&
            detail.field == "song",
        "and never reaches the speaker");
  rig.helper.speed = 1;
  check(rig.router.play(one(sound::Kind::Song, kHeldSong, true), sound::Group::App, "", detail) ==
            sound::PlayResult::Ok,
        "a song starts as music");
  check(waitFor([&] { return rig.helper.get(&FakeHelper::opens) == 1 &&
                             rig.helper.get(&FakeHelper::received) > 8820; }, 2000),
        "it plays through the mixer");
  bool loud = false;
  {
    std::lock_guard<std::mutex> lock(rig.helper.mutex);
    for (int16_t v : rig.helper.pcm) loud = loud || std::abs(v) > 3000;
  }
  check(loud, "and is heard");
  check(!rig.router.appSoundPlaying(), "a song, like a loop, is heard but never counts as playing");
  double first = 0.0, later = 0.0;
  check(waitFor([&] { return rig.sink->songBeat(monotonicMs(), first) && first > 0.05; }, 2000),
        "its beat counts what has been heard");
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  check(rig.sink->songBeat(monotonicMs(), later) && later > first + 0.2 && later < first + 0.6,
        "and moves on with the tempo");
  check(rig.router.play(one(sound::Kind::Song, kHeldSong, true), sound::Group::App, "", detail) ==
            sound::PlayResult::Ok,
        "the same song again");
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  double again = 0.0;
  check(rig.sink->songBeat(monotonicMs(), again) && again > later, "keeps playing where it was");

  check(rig.router.playEffect(one(sound::Kind::Song, kTickFx), "", detail) == sound::PlayResult::Ok &&
            rig.router.appSoundPlaying(),
        "a synth effect counts as playing from its request on");
  check(waitFor([&] { return !rig.router.appSoundPlaying(); }, 3000), "and ends by itself");

  check(rig.router.play(one(sound::Kind::Song, kTickFx), sound::Group::Alert, "", detail) == sound::PlayResult::Ok,
        "a song once as an alert");
  check(rig.sink->oneShotPlaying() && rig.router.alertPlaying() && rig.router.alertStatus().name == "song",
        "is the one-shot from its request on");
  check(waitFor([&] { return !rig.sink->oneShotPlaying(); }, 3000), "and ends by itself");

  rig.router.stop(sound::Stop::App);
  check(waitFor([&] { return rig.helper.get(&FakeHelper::stops) == 1; }, 1000),
        "stopping the app's sounds silences the song");
  check(waitFor([&] { double b; return !rig.sink->songBeat(monotonicMs(), b); }, 1000),
        "and its beat is gone");

  rig.sink->setVoiceOwned(true);
  check(rig.router.playEffect(one(sound::Kind::Song, kTickFx), "", detail) == sound::PlayResult::NoSink &&
            detail.message == "speaker unavailable",
        "no synth effect while the voice assistant holds the speaker");
  check(rig.router.play(one(sound::Kind::Song, kTickFx), sound::Group::Alert, "", detail) ==
            sound::PlayResult::NoSink,
        "and no song once");
  rig.sink->setVoiceOwned(false);
}

// music.pitch() from playback: a held A4 is heard as 440 Hz once the speaker plays it.
void test_sink_playback_pitch() {
  Rig rig;
  DispatchDetail detail;
  rig.helper.speed = 1;
  PlaybackPitch& pitch = rig.sink->playbackPitch();
  float hz = 0.f;
  check(!pitch.pitch(monotonicMs(), hz) && hz == 0.f, "no pitch while the speaker is silent");
  check(rig.router.play(one(sound::Kind::Song, kHeldSong, true), sound::Group::App, "", detail) ==
            sound::PlayResult::Ok,
        "a held note plays");
  check(waitFor([&] { return pitch.pitch(monotonicMs(), hz) && hz > 0.f; }, 3000) &&
            std::fabs(1200 * std::log2(hz / 440.0)) < 10,
        "the speaker's pitch is 440 Hz");
  rig.router.stop(sound::Stop::All);
  check(waitFor([&] { return !pitch.pitch(monotonicMs(), hz); }, 3000), "and goes once the speaker is silent");
}

void test_sink_layers() {
  Rig rig;
  DispatchDetail detail;
  check(rig.sink->mixes(), "the TC002 speaker mixes");
  rig.helper.speed = 1;
  check(rig.router.play(file("none", true), sound::Group::App, "", detail) == sound::PlayResult::NotFound,
        "unknown loop");
  check(rig.router.play(file("fx", true), sound::Group::App, "", detail) == sound::PlayResult::Ok, "a loop starts");
  check(waitFor([&] { return rig.helper.get(&FakeHelper::opens) == 1 &&
                             rig.helper.get(&FakeHelper::received) > 0; }, 2000),
        "the mixer opens a generation");
  check(!rig.router.appSoundPlaying(), "a loop alone is heard but never counts as playing");
  check(rig.helper.get(&FakeHelper::volume) == 20, "at the app level");

  check(rig.router.playEffect(file("fx"), "", detail) == sound::PlayResult::Ok, "an effect joins");
  check(rig.router.appSoundPlaying(), "an effect counts as playing from its request on");
  uint32_t ahead = 0;
  for (int i = 0; i < 100; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    std::lock_guard<std::mutex> lock(rig.helper.mutex);
    ahead = std::max(ahead, rig.helper.received - rig.helper.consumed);
  }
  const uint32_t lead = (MixSource::kLeadMs + 20) * 441 * 2 / 10;
  check(ahead > 0 && ahead <= lead, "the mixer queues no more than its lead");
  check(waitFor([&] { return !rig.router.appSoundPlaying(); }, 3000), "the effect ends, the loop plays on");

  check(rig.router.play(one(sound::Kind::Rtttl, "t:d=8,o=5,b=240:c,e"), sound::Group::App, "", detail) ==
            sound::PlayResult::Ok,
        "a melody over the loop");
  check(rig.sink->isPlaying(), "the melody plays");
  check(waitFor([&] { return !rig.sink->isPlaying(); }, 3000), "the melody ends");
  check(rig.router.play(file("fx"), sound::Group::App, "", detail) == sound::PlayResult::Ok, "an MP3 over the loop");
  check(waitFor([&] { return !rig.sink->oneShotPlaying(); }, 3000), "the MP3 ends");
  check(rig.helper.get(&FakeHelper::opens) == 1 && rig.helper.get(&FakeHelper::stops) == 0 &&
            rig.helper.get(&FakeHelper::drains) == 0,
        "one-shots, effects and the loop share one generation that outlives the loop's file");

  rig.router.stop(sound::Stop::App);
  check(waitFor([&] { return rig.helper.get(&FakeHelper::stops) == 1; }, 1000),
        "stopping the last layer silences at once");

  check(rig.router.playEffect(file("fx"), "", detail) == sound::PlayResult::Ok, "a lone effect");
  check(waitFor([&] { return rig.helper.get(&FakeHelper::opens) == 2; }, 2000), "opens a new mixer");
  check(waitFor([&] { return !rig.router.appSoundPlaying(); }, 3000) &&
            waitFor([&] { return rig.helper.get(&FakeHelper::drains) == 1; }, 3000),
        "and ends by itself, played out");

  check(rig.router.play(file("fx", true), sound::Group::App, "", detail) == sound::PlayResult::Ok, "a loop again");
  check(waitFor([&] { return rig.helper.get(&FakeHelper::opens) == 3 &&
                             rig.helper.get(&FakeHelper::received) > 0; }, 2000), "plays");
  const uint32_t stops = rig.helper.get(&FakeHelper::stops);
  rig.router.stop(sound::Stop::All);
  check(!rig.router.appSoundPlaying(), "stopping everything silences at once for the caller");
  check(waitFor([&] { return rig.helper.get(&FakeHelper::stops) == stops + 1; }, 1000),
        "and the helper stops it");
}

// Deleting a script's sounds stops what plays from them - the loop above all, which would play
// on for good - and nothing else.
void test_sink_releases_a_scripts_sounds() {
  writeAsset("/SCRIPTS/racer/theme.mp3", ksine_mono_64k_mp3, sizeof ksine_mono_64k_mp3);
  writeAsset("/SCRIPTS/racer/boost.mp3", ksine_mono_64k_mp3, sizeof ksine_mono_64k_mp3);
  Rig rig;
  DispatchDetail detail;
  rig.helper.speed = 1;
  check(rig.router.play(file("theme", true), sound::Group::App, "racer", detail) == sound::PlayResult::Ok,
        "the script's own loop starts");
  check(waitFor([&] { return rig.helper.get(&FakeHelper::received) > 0; }, 2000), "and plays");
  check(rig.router.play(file("boost"), sound::Group::App, "racer", detail) == sound::PlayResult::Ok,
        "the script's own one-shot starts");
  check(rig.sink->oneShotPlaying() && rig.router.appStatus().name == "boost", "state shows the one-shot by name");
  check(waitFor([&] { return !rig.sink->oneShotPlaying(); }, 3000), "and it ends");

  const uint32_t stops = rig.helper.get(&FakeHelper::stops);
  check(rig.router.play(file("boost"), sound::Group::App, "racer", detail) == sound::PlayResult::Ok,
        "the one-shot again");
  rig.router.release("/SCRIPTS/racer2");
  rig.router.release("/SCRIPTS/racer/theme");
  check(rig.sink->oneShotPlaying(), "a neighbour's name or a partial one releases nothing");
  rig.router.release("/SCRIPTS/racer/boost.mp3");
  check(!rig.sink->oneShotPlaying(), "releasing the one-shot's file stops it at once");
  rig.router.release("/SCRIPTS/racer");
  check(waitFor([&] { return rig.helper.get(&FakeHelper::stops) == stops + 1; }, 1000),
        "releasing the folder stops the loop, and with nothing left the helper stops");

  check(rig.router.play(file("fx", true), sound::Group::App, "", detail) == sound::PlayResult::Ok, "a shared loop");
  check(rig.router.playEffect(file("boost"), "racer", detail) == sound::PlayResult::Ok,
        "and the script's effect over it");
  check(waitFor([&] { return rig.router.appSoundPlaying(); }, 1000), "the effect plays");
  rig.router.release("/SCRIPTS/racer");
  check(waitFor([&] { return !rig.router.appSoundPlaying(); }, 1000), "releasing the folder drops the effect");
  rig.router.stop(sound::Stop::App);
}

void test_sink_stop_and_nonblocking() {
  Rig rig;
  DispatchDetail detail;
  const sound::Choices longMelody = one(sound::Kind::Rtttl, "long:d=1,o=5,b=60:c,d,e,f,g,a,b");
  check(rig.router.play(longMelody, sound::Group::Alert, "", detail) == sound::PlayResult::Ok, "start");
  check(waitFor([&] { return rig.helper.get(&FakeHelper::received) > 0; }, 2000), "audio flowing");
  const int64_t asked = monotonicMs();
  rig.router.stop(sound::Stop::All);
  check(!rig.sink->isPlaying(), "stopped at once for the caller");
  check(waitFor([&] { return rig.helper.get(&FakeHelper::stops) == 1; }, 200), "STOP reaches the helper");
  check(monotonicMs() - asked < 200, "within 200 ms");

  rig.helper.stalled = true;
  check(rig.router.play(longMelody, sound::Group::Alert, "", detail) == sound::PlayResult::Ok,
        "plays against a stalled helper");
  int64_t worst = 0;
  for (int i = 0; i < 2000; ++i) {
    const int64_t started = awtrix::monotonicUs();
    rig.tick();
    rig.router.setVolumes(i % 100, 30, 20, 10);
    rig.router.alertPlaying();
    worst = std::max(worst, awtrix::monotonicUs() - started);
  }
  check(worst < 20000, "render-thread calls never wait on the helper");
  rig.helper.stalled = false;
}

void test_sink_helper_death() {
  Rig rig;
  DispatchDetail detail;
  rig.helper.stopThread();
  ::shutdown(rig.helper.fd, SHUT_RDWR);
  check(waitFor([&] { return !rig.sink->available(); }, 2000), "a dead helper is noticed");
  check(rig.router.play(one(sound::Kind::Rtttl, "t:d=4,o=5,b=120:c"), sound::Group::Alert, "", detail) ==
                sound::PlayResult::NoSink &&
            detail.message == "speaker unavailable",
        "nothing plays afterwards, and a valid melody is no mistake");
  check(rig.router.playStream("http://127.0.0.1:1/x", "x", detail) == DispatchResult::Unavailable,
        "stations are refused");
}

void test_sink_helper_failure_before_hello() {
  Rig rig(false);
  {
    std::lock_guard<std::mutex> lock(rig.helper.mutex);
    rig.helper.status(TC002_AUDIO_FAILED, TC002_AUDIO_ERROR_PREFLIGHT);
  }
  check(waitFor([&] { return !rig.sink->available(); }, 2000), "a preflight failure disables the sink");
}

// A minimal Icecast-style server: a playlist, and an endless MP3 with ICY metadata.
struct IcyServer {
  int listener = -1;
  int port = 0;
  std::atomic<bool> running{true};
  std::atomic<int> streams{0};
  std::atomic<int> sounds{0};
  std::atomic<bool> sawIcyHeader{false};
  bool metadata = true;
  std::atomic<bool> rejectFirst{false};
  std::thread thread;
  std::vector<std::thread> clients;

  explicit IcyServer(bool withMetadata = true, bool firstFails = false)
      : metadata(withMetadata), rejectFirst(firstFails) {
    listener = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    check(::bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof address) == 0, "bind");
    socklen_t length = sizeof address;
    ::getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length);
    port = ntohs(address.sin_port);
    check(::listen(listener, 8) == 0, "listen");
    thread = std::thread([this] {
      while (running) {
        pollfd p{listener, POLLIN, 0};
        if (::poll(&p, 1, 20) <= 0) continue;
        const int client = ::accept4(listener, nullptr, nullptr, SOCK_CLOEXEC);
        if (client >= 0) clients.emplace_back([this, client] { serve(client); });
      }
    });
  }

  ~IcyServer() {
    running = false;
    thread.join();
    for (auto& client : clients) client.join();
    ::close(listener);
  }

  bool write(int fd, const void* data, std::size_t size) {
    const char* p = static_cast<const char*>(data);
    while (size && running) {
      pollfd w{fd, POLLOUT, 0};
      if (::poll(&w, 1, 20) <= 0) continue;
      const ssize_t n = ::send(fd, p, size, MSG_NOSIGNAL);
      if (n <= 0) return false;
      p += n;
      size -= static_cast<std::size_t>(n);
    }
    return running;
  }

  void serve(int fd) {
    std::string request;
    char buffer[1024];
    while (request.find("\r\n\r\n") == std::string::npos && running) {
      const ssize_t n = ::recv(fd, buffer, sizeof buffer, 0);
      if (n <= 0) break;
      request.append(buffer, static_cast<std::size_t>(n));
    }
    if (request.find("Icy-MetaData: 1") != std::string::npos) sawIcyHeader = true;
    if (request.rfind("GET /sound.mp3", 0) == 0) {
      ++sounds;
      const std::string head = "HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\nContent-Length: " +
                               std::to_string(sizeof ksine_mono_64k_mp3) + "\r\nConnection: close\r\n\r\n";
      write(fd, head.data(), head.size());
      write(fd, ksine_mono_64k_mp3, sizeof ksine_mono_64k_mp3);
    } else if (request.rfind("GET /missing.mp3", 0) == 0) {
      const std::string head = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
      write(fd, head.data(), head.size());
    } else if (request.rfind("GET /list.m3u", 0) == 0 || request.rfind("GET /list.pls", 0) == 0 ||
               request.rfind("GET /playlist", 0) == 0) {
      const bool pls = request.rfind("GET /list.pls", 0) == 0;
      const std::string body = std::string(pls ? "[playlist]\nFile1=" : "#EXTM3U\n") +
                               "http://127.0.0.1:" + std::to_string(port) + "/stream\n";
      const std::string type = pls ? "audio/x-scpls" :
                              (request.rfind("GET /playlist", 0) == 0 ? "audio/mpegurl" : "audio/x-mpegurl");
      const std::string head = "HTTP/1.1 200 OK\r\nContent-Type: " + type + "\r\nContent-Length: " +
                               std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n";
      write(fd, head.data(), head.size());
      write(fd, body.data(), body.size());
    } else if (request.rfind("GET /redirect-lf", 0) == 0) {
      const std::string head = "HTTP/1.1 302 Found\r\nLocation: /lf-stream\r\nContent-Length: 0\r\n"
                               "Connection: close\r\n\r\n";
      write(fd, head.data(), head.size());
    } else if (request.rfind("GET /query.m3u", 0) == 0) {
      const std::string body = "http://127.0.0.1:" + std::to_string(port) +
                               "/stream?sABC=x%3D&aw_0_1st.playerid=web&amsparams=playerid:web;skey:1\n";
      const std::string head = "HTTP/1.1 200 OK\r\nContent-Type: audio/x-mpegurl\r\nContent-Length: " +
                               std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n";
      write(fd, head.data(), head.size());
      write(fd, body.data(), body.size());
    } else if (request.rfind("GET /broken", 0) == 0) {
      const std::string body(48 * 1024, '\0');
      const std::string head = "HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\nContent-Length: " +
                               std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n";
      write(fd, head.data(), head.size());
      write(fd, body.data(), body.size());
    } else if (request.rfind("GET /stream", 0) == 0 || request.rfind("GET /lf-stream", 0) == 0 ||
               request.rfind("GET /icy-stream", 0) == 0 || request.rfind("GET /lf-all", 0) == 0) {
      if (rejectFirst.exchange(false)) {
        const std::string error = "HTTP/1.1 503 Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        write(fd, error.data(), error.size());
        ::close(fd);
        return;
      }
      ++streams;
      const char* status = request.rfind("GET /lf-stream", 0) == 0    ? "HTTP/1.1 200 OK\n"
                           : request.rfind("GET /icy-stream", 0) == 0 ? "ICY 200 OK\r\n"
                                                                     : "HTTP/1.1 200 OK\r\n";
      const char* eol = request.rfind("GET /lf-all", 0) == 0 ? "\n" : "\r\n";
      if (*eol == '\n') status = "ICY 200 OK\n";
      const std::string head = std::string(status) + "Content-Type: audio/mpeg" + eol +
                               (metadata ? std::string("icy-metaint: 4096") + eol : std::string()) + eol;
      std::string meta = "StreamTitle='Test Title';";
      meta.resize((meta.size() + 15) / 16 * 16, '\0');
      bool okay = write(fd, head.data(), head.size());
      std::size_t at = 0;
      while (okay && running) {
        std::string chunk;
        for (int i = 0; i < 4096; ++i) chunk.push_back(static_cast<char>(ksine_stereo_128k_mp3[at++ % sizeof ksine_stereo_128k_mp3]));
        if (metadata) {
          chunk.push_back(static_cast<char>(meta.size() / 16));
          chunk += meta;
        }
        okay = write(fd, chunk.data(), chunk.size());
      }
    }
    ::close(fd);
  }
};

void test_stream_playlists_and_wrapped_decode() {
  IcyServer server;
  const std::string base = "http://127.0.0.1:" + std::to_string(server.port);
  for (const auto* path : {"/list.m3u", "/list.pls", "/playlist"}) {
    {
      StreamSource source(base + path);
      unsigned decoded = 0;
      check(waitFor([&] {
        const int16_t* pcm = nullptr;
        std::size_t frames = 0;
        for (unsigned i = 0; i < 32; ++i) {
          const auto result = source.next(pcm, frames);
          check(result != PcmSource::Read::Error, "playlist stream remains decodable");
          if (result != PcmSource::Read::Data) break;
          check(pcm && frames && source.rate() == 44100 && source.channels() == 2,
                "wrapped input keeps the decoded format and PCM");
          ++decoded;
        }
        return decoded >= 400;
      }, 1000), "playlist audio continues through repeated buffer wraps");
      std::string title;
      check(source.takeTitle(title) && title == "Test Title", "wrapped audio preserves ICY metadata");
    }
    check(StreamSource::waitForNetworkThreads(1000), "station switch releases the previous connection");
  }
}

// Status lines as streamABC/regiocast (bare LF, also behind a redirect) and Shoutcast v1 (ICY) send
// them, a response with bare LF line ends throughout, and a playlist whose stream URL carries a
// query with "=".
void test_stream_accepts_station_server_variants() {
  IcyServer server;
  const std::string base = "http://127.0.0.1:" + std::to_string(server.port);
  for (const auto* path : {"/lf-stream", "/redirect-lf", "/icy-stream", "/lf-all", "/query.m3u"}) {
    {
      StreamSource source(base + path);
      unsigned decoded = 0;
      check(waitFor([&] {
        const int16_t* pcm = nullptr;
        std::size_t frames = 0;
        while (decoded < 100 && source.next(pcm, frames) == PcmSource::Read::Data) ++decoded;
        return decoded >= 100;
      }, 1000), std::string(path) + " plays");
      std::string title;
      check(source.takeTitle(title) && title == "Test Title", std::string(path) + " keeps its ICY title");
    }
    check(StreamSource::waitForNetworkThreads(1000), "each variant releases its connection");
  }
}

void test_stream_rejects_non_mp3_input() {
  IcyServer server(false);
  {
    StreamSource source("http://127.0.0.1:" + std::to_string(server.port) + "/broken");
    check(waitFor([&] {
      const int16_t* pcm = nullptr;
      std::size_t frames = 0;
      return source.next(pcm, frames) == PcmSource::Read::Error;
    }, 1000), "non-MP3 input fails instead of waiting on a full buffer");
    std::string error;
    check(source.takeError(error) && !error.empty(), "failed decoding reports an error");
  }
  check(StreamSource::waitForNetworkThreads(1000), "failed station cancels its retry wait");
}

// A listener whose accept queue is full drops SYNs, like a host that is down: a connect to it
// hangs until the client's connect timeout.
void test_stream_drop_while_connecting() {
  const int listener = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  check(::bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof address) == 0 && ::listen(listener, 0) == 0,
        "listener with no backlog");
  socklen_t length = sizeof address;
  ::getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length);
  const int filler = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
  check(::connect(filler, reinterpret_cast<sockaddr*>(&address), sizeof address) == 0, "the accept queue fills");
  const int probe = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  ::connect(probe, reinterpret_cast<sockaddr*>(&address), sizeof address);
  pollfd p{probe, POLLOUT, 0};
  check(::poll(&p, 1, 200) == 0, "further connects hang");
  ::close(probe);

  std::unique_ptr<StreamSource> source(
      new StreamSource("http://127.0.0.1:" + std::to_string(ntohs(address.sin_port)) + "/"));
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  const int64_t started = monotonicMs();
  source.reset();
  check(monotonicMs() - started < 100, "dropping a station never waits for its connect");
  check(StreamSource::waitForNetworkThreads(1000), "and its network thread abandons the connect");
  ::close(filler);
  ::close(listener);
}

void test_sink_station() {
  IcyServer server;
  Rig rig;
  DispatchDetail detail;
  const std::string base = "http://127.0.0.1:" + std::to_string(server.port);
  check(rig.router.playStream("ftp://nope", "x", detail) == DispatchResult::ValidationError, "bad URL");
  check(rig.router.playStream(base + "/list.m3u", "Test", detail) == DispatchResult::Ok, "station accepted");
  check(waitFor([&] { return rig.helper.get(&FakeHelper::received) > 16384; }, 5000), "station audio arrives");
  check(server.sawIcyHeader, "metadata requested");
  check(rig.helper.get(&FakeHelper::rate) == 44100 && rig.helper.get(&FakeHelper::channels) == 2, "decoded format");
  check(rig.helper.get(&FakeHelper::volume) == 30, "a station plays at the radio level");
  check(waitFor([&] { rig.tick(); return rig.engine.state().runtime().radioTitle == "Test Title"; }, 5000),
        "ICY title reaches the runtime state");
  check(rig.sink->bufferBytes() > 0, "buffer telemetry");

  rig.helper.stalled = true;
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  uint32_t lost = 0;
  {
    std::lock_guard<std::mutex> lock(rig.helper.mutex);
    rig.helper.read();
    lost = rig.helper.generation;
    rig.helper.primeTimeout();
  }
  rig.helper.stalled = false;
  check(waitFor([&] { return rig.helper.get(&FakeHelper::generation) != lost &&
                             rig.helper.get(&FakeHelper::received) > 0; }, 3000),
        "a station whose generation the helper ended opens a new one and keeps playing");
  rig.tick();
  check(rig.engine.state().runtime().radioError.empty(), "without reporting the station as failed");

  const uint32_t opens = rig.helper.get(&FakeHelper::opens);
  check(rig.router.play(one(sound::Kind::Rtttl, "t:d=8,o=5,b=240:c,e"), sound::Group::Alert, "", detail) ==
            sound::PlayResult::Ok,
        "a melody interrupts the station");
  check(waitFor([&] { return rig.helper.get(&FakeHelper::opens) >= opens + 1 &&
                             rig.helper.get(&FakeHelper::channels) == 1; }, 2000), "melody generation");
  check(waitFor([&] { return !rig.sink->isPlaying(); }, 3000), "melody ends");
  check(waitFor([&] { return rig.helper.get(&FakeHelper::channels) == 2 && server.streams >= 2; }, 8000),
        "the station reconnects afterwards");
  audio::FrameStats stats;
  rig.sink->analysis(monotonicMs(), stats);
  check(waitFor([&] { return rig.sink->analysis(monotonicMs(), stats); }, 3000), "visualizers get the station");
  const uint32_t stops = rig.helper.get(&FakeHelper::stops);
  rig.router.stop(sound::Stop::Radio);
  check(waitFor([&] { return rig.helper.get(&FakeHelper::stops) > stops; }, 500), "stopping the station stops output");

  check(rig.router.play(file("fx", true), sound::Group::App, "", detail) == sound::PlayResult::Ok, "a loop");
  check(waitFor([&] { return rig.helper.get(&FakeHelper::channels) == 1 &&
                             rig.helper.get(&FakeHelper::received) > 0; }, 2000), "plays");
  const int streams = server.streams;
  check(rig.router.playStream(base + "/list.m3u", "Test", detail) == DispatchResult::Ok,
        "a station asked for during the loop");
  check(waitFor([&] { return rig.helper.get(&FakeHelper::channels) == 2 && server.streams > streams; }, 8000),
        "ends the loop and plays");
  rig.router.stop(sound::Stop::Radio);

  const int closed = ::socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  socklen_t length = sizeof address;
  ::bind(closed, reinterpret_cast<sockaddr*>(&address), sizeof address);
  ::getsockname(closed, reinterpret_cast<sockaddr*>(&address), &length);
  ::close(closed);
  const std::string dead = "http://127.0.0.1:" + std::to_string(ntohs(address.sin_port)) + "/";
  check(rig.router.playStream(dead, "Dead", detail) == DispatchResult::Ok, "unreachable station accepted");
  check(waitFor([&] { rig.tick(); return rig.engine.state().runtime().radioError == "connect failed"; }, 3000),
        "connection failures reach the runtime state");
  rig.router.stop(sound::Stop::Radio);
}

void test_station_recovers_without_metadata() {
  IcyServer server(false, true);
  Rig rig;
  DispatchDetail detail;
  const std::string url = "http://127.0.0.1:" + std::to_string(server.port) + "/stream";
  check(rig.router.playStream(url, "Retry", detail) == DispatchResult::Ok, "retry station accepted");
  check(waitFor([&] {
    rig.tick();
    return rig.engine.state().runtime().radioError == "connect failed";
  }, 1500), "the first failed connection is reported");
  check(waitFor([&] {
    rig.tick();
    const auto& state = rig.engine.state().runtime();
    return rig.helper.get(&FakeHelper::received) > 16384 && state.radioPlaying && state.radioError.empty();
  }, 5000), "reconnected audio clears the error and restores playing without metadata");
  check(rig.engine.state().runtime().radioTitle.empty(), "recovery did not depend on an ICY title");
  rig.router.stop(sound::Stop::Radio);
}

// The voice assistant takes the speaker from a station, which plays again afterwards.
void test_voice_keeps_the_station() {
  IcyServer server;
  Rig rig;
  DispatchDetail detail;
  const std::string url = "http://127.0.0.1:" + std::to_string(server.port) + "/stream";
  check(rig.router.playStream(url, "Test", detail) == DispatchResult::Ok, "station accepted");
  check(waitFor([&] { rig.tick(); return rig.engine.state().runtime().radioPlaying &&
                                         rig.helper.get(&FakeHelper::received) > 0; }, 5000),
        "the station plays");
  const uint32_t stops = rig.helper.get(&FakeHelper::stops);
  rig.sink->setVoiceOwned(true);
  check(waitFor([&] { return rig.sink->voiceQuiet(); }, 2000) && rig.helper.get(&FakeHelper::stops) > stops,
        "the voice takes the speaker from the station");
  const int streams = server.streams;
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  rig.tick();
  check(server.streams == streams && rig.engine.state().runtime().radioPlaying,
        "the station waits meanwhile and still counts as playing");
  rig.sink->setVoiceOwned(false);
  check(waitFor([&] { return server.streams > streams && rig.helper.get(&FakeHelper::channels) == 2; }, 8000),
        "and plays again once the voice lets go");
  rig.router.stop(sound::Stop::Radio);
}

// A looping alert keeps the station away between its plays instead of letting it reconnect in
// every gap, and counts as playing throughout; the station comes back once the alert is stopped.
void test_a_repeating_alert_keeps_the_station_away() {
  IcyServer server;
  Rig rig;
  DispatchDetail detail;
  const std::string url = "http://127.0.0.1:" + std::to_string(server.port) + "/stream";
  check(rig.router.playStream(url, "Test", detail) == DispatchResult::Ok, "station accepted");
  check(waitFor([&] { return rig.helper.get(&FakeHelper::channels) == 2 &&
                             rig.helper.get(&FakeHelper::received) > 0; }, 5000), "the station plays");
  const uint32_t opens = rig.helper.get(&FakeHelper::opens);
  check(rig.router.play(one(sound::Kind::Rtttl, "t:d=16,o=5,b=240:c", true), sound::Group::Alert, "",
                        detail) == sound::PlayResult::Ok,
        "a looping alert");
  const int streams = server.streams;
  bool steady = true;
  check(waitFor([&] {
    rig.tick();
    steady = steady && rig.router.alertStatus().playing;
    return rig.helper.get(&FakeHelper::opens) >= opens + 3;
  }, 5000), "repeats");
  check(steady && server.streams == streams, "with the station away and the alert playing throughout");
  rig.router.stop(sound::Stop::Alert);
  check(waitFor([&] { return server.streams > streams && rig.helper.get(&FakeHelper::channels) == 2; }, 8000),
        "the station comes back once it is stopped");
  rig.router.stop(sound::Stop::Radio);
}

// The voice assistant silences a script's music, which waits like a station and starts over once
// the voice lets go, also after the answer played.
void test_the_voice_holds_the_music() {
  Rig rig;
  DispatchDetail detail;
  check(rig.router.play(file("fx", true), sound::Group::App, "player", detail) == sound::PlayResult::Ok &&
            waitFor([&] { return rig.helper.get(&FakeHelper::received) > 0; }, 2000),
        "a script's music plays");
  rig.sink->setVoiceOwned(true);
  check(waitFor([&] { return rig.sink->voiceQuiet(); }, 2000), "the voice silences it");
  rig.tick();
  check(rig.sink->loopPlaying() && rig.router.appStatus().playing, "and it still counts as playing");
  check(rig.router.play(one(sound::Kind::Rtttl, "t:d=4,o=5,b=120:c"), sound::Group::Alert, "", detail) ==
                sound::PlayResult::NoSink &&
            detail.message == "speaker unavailable",
        "a melody meanwhile finds the speaker taken, no mistake in it");
  check(rig.sink->playSystemSound(host::hostPath("/MP3/sine.mp3"), 0) &&
            waitFor([&] { return rig.sink->systemFinished(); }, 5000),
        "the answer plays to its end");
  check(waitFor([&] { return rig.sink->voiceQuiet(); }, 2000) && rig.sink->loopPlaying(),
        "alone, the music still waiting");
  const uint32_t opens = rig.helper.get(&FakeHelper::opens);
  rig.sink->setVoiceOwned(false);
  check(waitFor([&] { return rig.helper.get(&FakeHelper::opens) > opens &&
                             rig.helper.get(&FakeHelper::received) > 0; }, 2000),
        "the music plays again once the voice lets go");
  rig.router.stop(sound::Stop::All);
}

// Music a script asks for while the voice holds the speaker is the one that comes back; music it
// stops meanwhile stays off.
void test_music_asked_during_the_voice() {
  Rig rig;
  DispatchDetail detail;
  check(rig.router.play(file("fx", true), sound::Group::App, "player", detail) == sound::PlayResult::Ok &&
            waitFor([&] { return rig.helper.get(&FakeHelper::received) > 0; }, 2000),
        "a script's music plays");
  rig.sink->setVoiceOwned(true);
  check(waitFor([&] { return rig.sink->voiceQuiet(); }, 2000), "the voice silences the music");
  check(rig.router.play(one(sound::Kind::Song, kHeldSong, true), sound::Group::App, "player", detail) ==
            sound::PlayResult::Ok,
        "a new song meanwhile is accepted");
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  check(rig.sink->voiceQuiet(), "and waits");
  rig.sink->setVoiceOwned(false);
  check(waitFor([&] { double beat; return rig.sink->songBeat(monotonicMs(), beat); }, 2000),
        "the song plays once the voice lets go");

  rig.sink->setVoiceOwned(true);
  check(waitFor([&] { return rig.sink->voiceQuiet(); }, 2000), "the voice silences it again");
  rig.router.stop(sound::Stop::ScriptMusic, "player");
  check(!rig.sink->loopPlaying(), "the script stops its music meanwhile");
  const uint32_t opens = rig.helper.get(&FakeHelper::opens);
  rig.sink->setVoiceOwned(false);
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  check(rig.helper.get(&FakeHelper::opens) == opens, "and it stays off");
}

// The boot sound and the voice's answer are alerts: no app sound cuts them off.
void test_a_system_sound_is_an_alert() {
  Rig rig;
  DispatchDetail detail;
  rig.helper.speed = 1;
  check(rig.sink->playSystemSound(host::hostPath("/MP3/sine.mp3"), 0), "a system sound");
  check(rig.sink->oneShotPlaying(), "counts as the one-shot");
  rig.tick();
  check(rig.router.alertPlaying() && rig.router.alertStatus().playing, "of the alert group");
  check(rig.router.play(file("fx"), sound::Group::App, "player", detail) == sound::PlayResult::Ok,
        "a script's sound meanwhile is accepted");
  int64_t audibleAt = -1;
  check(rig.sink->systemStart(audibleAt) != Tc002AudioSink::SystemStart::Failed && !rig.sink->systemFinished(),
        "and leaves the system sound playing");
  rig.sink->stopSystemSound();
}

// A sound from an address: fetched, then played as the one-shot; a looping one repeats from the one
// download, and a failed fetch is the alert's error.
void test_a_url_one_shot_plays() {
  IcyServer server;
  Rig rig;
  DispatchDetail detail;
  const std::string base = "http://127.0.0.1:" + std::to_string(server.port);
  rig.helper.speed = 4;
  check(rig.router.play(file(base + "/sound.mp3"), sound::Group::Alert, "", detail) == sound::PlayResult::Ok,
        "an address is accepted");
  check(rig.sink->oneShotPlaying() && rig.router.alertPlaying(), "and counts as playing while it is fetched");
  check(waitFor([&] { rig.tick(); return rig.helper.get(&FakeHelper::received) > 0; }, 5000),
        "the fetched file reaches the speaker");
  check(rig.router.alertStatus().name == base + "/sound.mp3", "the state names the address");
  check(waitFor([&] { rig.tick(); return !rig.sink->oneShotPlaying(); }, 5000), "it ends by itself");
  check(server.sounds == 1, "fetched once");

  Mp3FileSource decoded(host::hostPath("/MP3/fx.mp3"));
  const uint64_t once = drainSource(decoded).size() * sizeof(int16_t);
  const uint64_t before = rig.helper.get(&FakeHelper::total);
  check(rig.router.play(file(base + "/sound.mp3", true), sound::Group::Alert, "", detail) == sound::PlayResult::Ok,
        "a looping alert");
  check(waitFor([&] { rig.tick(); return rig.helper.get(&FakeHelper::total) >= before + 3 * once; }, 10000),
        "repeats");
  check(server.sounds == 1, "from the file it fetched before");
  rig.router.stop(sound::Stop::Alert);
  check(!rig.router.alertPlaying(), "until the alerts are stopped");

  check(rig.router.play(file(base + "/missing.mp3"), sound::Group::Alert, "", detail) == sound::PlayResult::Ok,
        "a missing file is accepted");
  check(waitFor([&] { rig.tick(); return rig.router.alertStatus().error == "HTTP 404"; }, 5000),
        "its status is the alert's error");
  check(!rig.sink->oneShotPlaying(), "and nothing plays");
}

void test_sink_station_without_helper() {
  IcyServer server;
  const std::string url = "http://127.0.0.1:" + std::to_string(server.port) + "/stream";
  {
    Rig rig;
    DispatchDetail detail;
    check(rig.router.playStream(url, "Test", detail) == DispatchResult::Ok, "station accepted");
    check(waitFor([&] { rig.tick(); return rig.engine.state().runtime().radioPlaying; }, 5000),
          "the station plays");
    rig.helper.stopThread();
    // Once the credit window is full nothing is sent, so the death shows as the socket's end.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    ::shutdown(rig.helper.fd, SHUT_RDWR);
    check(waitFor([&] { rig.tick(); return !rig.engine.state().runtime().radioPlaying; }, 2000),
          "a station whose helper died no longer counts as playing");
    check(rig.engine.state().runtime().radioError == "speaker unavailable", "and says why");
  }
  {
    Rig rig(false);
    DispatchDetail detail;
    check(rig.router.playStream(url, "Test", detail) == DispatchResult::Ok, "station accepted before hello");
    check(waitFor([&] { rig.tick(); return rig.engine.state().runtime().radioPlaying; }, 4000),
          "the station connects while the helper is silent");
    check(waitFor([&] { rig.tick(); return !rig.engine.state().runtime().radioPlaying; },
                  static_cast<int>(Tc002AudioPlayer::kReadyTimeoutMs) + 2000),
          "a station whose helper never said hello no longer counts as playing");
    check(rig.engine.state().runtime().radioError == "speaker unavailable", "and says why");
  }
}

}

int main() {
  const std::string dataDir = tempDir();
  host::setDataDir(dataDir);
  test_protocol_from_cpp();
  test_link_credit_and_control();
  test_link_eof_and_nonblocking();
  test_tone_source();
  test_player_tone_drain_and_stop();
  test_player_generation_ended_by_helper();
  test_player_ready_timeout();
  test_player_mp3(dataDir);
  test_voice_wav_and_ownership();
  test_clip_sources();
  test_sink_clips();
  test_sink_speech();
  test_sink_tone_mp3_and_router();
  test_mix_source();
  test_mix_buffer_boundaries();
  test_mix_source_synth();
  test_limiter();
  test_mix_source_overlap();
  test_volume_zero_silences_a_playing_mix();
  test_an_alert_over_app_music();
  test_sink_layers();
  test_sink_songs();
  test_sink_playback_pitch();
  test_sink_releases_a_scripts_sounds();
  test_sink_stop_and_nonblocking();
  test_sink_helper_death();
  test_sink_helper_failure_before_hello();
  test_stream_drop_while_connecting();
  test_station_recovers_without_metadata();
  test_sink_station();
  test_stream_playlists_and_wrapped_decode();
  test_stream_accepts_station_server_variants();
  test_stream_rejects_non_mp3_input();
  test_voice_keeps_the_station();
  test_a_repeating_alert_keeps_the_station_away();
  test_the_voice_holds_the_music();
  test_music_asked_during_the_voice();
  test_a_system_sound_is_an_alert();
  test_a_url_one_shot_plays();
  test_sink_station_without_helper();
  check(StreamSource::waitForNetworkThreads(3000), "network threads end after their source");
  check(speech::SpeechSource::waitForWorkers(3000), "speech workers end after their source");
  std::error_code error;
  std::filesystem::remove_all(dataDir, error);
  std::printf("tc002 audio sink contracts: %u passed\n", passed.load());
  return 0;
}
