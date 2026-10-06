#include "system/AudioOutEsp32.h"

#if defined(AWTRIX_SOC_ESP32S3)

#include <Arduino.h>
#include <LittleFS.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <driver/i2s.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>

#include <cstdlib>
#include <memory>
#include <vector>

#include "core/audio/Limiter.h"
#include "core/audio/Mp3FileDecoder.h"
#include "core/audio/StreamDecode.h"
#include "core/audio/StreamInputBuffer.h"
#include "core/radio/StationUrl.h"
#include "core/radio/StreamPolicy.h"
#include "core/sound/SoundMp3.h"
#include "core/script/ScriptServices.h"
#include "system/HeapCaps.h"
#include "system/HeapProbe.h"
#include "system/Log.h"
#include "system/MonotonicClock.h"

namespace awtrix {

namespace {

constexpr uint32_t kTaskStackBytes = 12288;
constexpr UBaseType_t kTaskPriority = 2;
constexpr BaseType_t kTaskCore = 1;

constexpr int kDmaBufferCount = 8;
constexpr int kDmaBufferFrames = 512;

// When i2s_write returns, (kDmaBufferCount - 1) buffers sit between the end of the frame just
// handed over and the speaker. kAudibleTrimMs is the knob for what that model gets wrong.
constexpr int kQueueAheadFrames = (kDmaBufferCount - 1) * kDmaBufferFrames;
constexpr int kAudibleTrimMs = 0;

int64_t audibleLeadMs(int frameSamples, int rateHz) {
  return (static_cast<int64_t>(kQueueAheadFrames - frameSamples) * 1000) / rateHz +
         kAudibleTrimMs;
}

constexpr std::size_t kNetworkChunkBytes = 1024;

// Capped per pass so the loop keeps reading the socket; flat out here starves the input.
constexpr int kFramesPerPass = 2;

constexpr int kMaxRedirects = 3;
constexpr uint32_t kConnectTimeoutMs = 8000;

const char* kUserAgent = "AWTRIX-NG";

// Songs render at 44.1 kHz with a song's full polyphony, in blocks of an MP3 frame: the spectrum
// analysis runs once per block.
constexpr uint32_t kSongRate = 44100;
constexpr std::size_t kSongVoices = 24;
constexpr int kSongBlockFrames = mp3::kMaxSamplesPerFrame;

// The task notices a stop within a frame; the bound only matters for a wedged I2S driver.
constexpr int kReleaseWaitMs = 500;
constexpr int kReleasePollMs = 5;

// A parsed song is mostly small note lists, each below the size plain malloc steers to PSRAM.
void* allocateSong(std::size_t bytes) {
  if (void* p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)) return p;
  return std::malloc(bytes);
}

}

bool AudioOutEsp32::usable(int pinBclk, int pinLrclk, int pinDout) {
  if (pinBclk < 0 || pinLrclk < 0 || pinDout < 0) return false;
  return psramFound();
}

// Pinned to internal RAM: PSRAM latency on the per-frame decoder state costs decode time.
void* AudioOutEsp32::operator new(std::size_t bytes) {
  if (void* p = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)) return p;
  return std::malloc(bytes);
}

void AudioOutEsp32::operator delete(void* p) { std::free(p); }

AudioOutEsp32::AudioOutEsp32(CoreEngine& engine, int pinBclk, int pinLrclk, int pinDout,
                             int pinMclk, int pinAmpEnable)
    : engine_(engine),
      pinBclk_(pinBclk),
      pinLrclk_(pinLrclk),
      pinDout_(pinDout),
      pinMclk_(pinMclk),
      pinAmpEnable_(pinAmpEnable) {
  lock_ = xSemaphoreCreateMutex();
  synth::setSongMemory({allocateSong, heap_caps_free});
  // Driven low until the first stream installs the driver: floating clock lines make the
  // amplifier crackle, a still BCLK sends it to sleep.
  for (int pin : {pinBclk_, pinLrclk_, pinDout_, pinMclk_}) {
    if (pin < 0) continue;
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
  }
  // Held high for good: the amplifier's own mute click is worse than its idle noise, and a
  // notification sound must not wait for it to come up.
  if (pinAmpEnable_ >= 0) {
    pinMode(pinAmpEnable_, OUTPUT);
    digitalWrite(pinAmpEnable_, HIGH);
  }
}

AudioOutEsp32::~AudioOutEsp32() {
  stopStream();
  if (task_) vTaskDelete(task_);
  if (lock_) vSemaphoreDelete(lock_);
}

bool AudioOutEsp32::ensureTask() {
  if (task_) return true;
  return xTaskCreatePinnedToCore(taskEntry, "audio", kTaskStackBytes, this, kTaskPriority, &task_,
                                 kTaskCore) == pdPASS;
}

DispatchResult AudioOutEsp32::playStream(const std::string& url, const std::string& label,
                                         DispatchDetail& detail) {
  radio::Url parsed;
  if (!radio::parseUrl(url, parsed)) {
    detail = {"url", "invalid URL"};
    return DispatchResult::ValidationError;
  }

  if (parsed.tls) {
    const std::size_t free = heap_caps_get_free_size(kGuardHeapCaps);
    const std::size_t largest = heap_caps_get_largest_free_block(kGuardHeapCaps);
    if (!script::fetchFits(true, free, largest)) {
      detail = {"url", "not enough memory for TLS"};
      return DispatchResult::Busy;
    }
  }

  if (xSemaphoreTake(lock_, portMAX_DELAY) == pdTRUE) {
    pendingUrl_ = url;
    pendingLabel_ = label;
    pendingError_.clear();
    // The switch-station signal: the task compares it every pass.
    urlSeq_.fetch_add(1);
    xSemaphoreGive(lock_);
  }
  stopRequested_.store(false);

  if (!ensureTask()) {
    detail = {"", "audio task failed"};
    return DispatchResult::Failed;
  }
  return DispatchResult::Ok;
}

bool AudioOutEsp32::checkSong(const std::string& text, std::string& error) {
  const synth::ParseResult parsed = songs_.get(text);
  if (!parsed.ok()) error = parsed.describe();
  return parsed.ok();
}

bool AudioOutEsp32::playSongOnce(const std::string& text, sound::Group group) {
  const synth::ParseResult parsed = songs_.get(text);
  if (!parsed.ok()) return false;
  if (xSemaphoreTake(lock_, portMAX_DELAY) == pdTRUE) {
    pendingMp3_.clear();
    pendingSong_ = parsed.song;
    pendingGroup_ = group;
    mp3Pending_.store(true);
    mp3Stop_.store(false);
    mp3Seq_.fetch_add(1);
    xSemaphoreGive(lock_);
  }
  return ensureTask();
}

bool AudioOutEsp32::playMp3(const std::string& path, sound::Group group) {
  if (xSemaphoreTake(lock_, portMAX_DELAY) == pdTRUE) {
    pendingMp3_ = path;
    pendingSong_.reset();
    pendingGroup_ = group;
    mp3Pending_.store(true);
    mp3Stop_.store(false);
    mp3Seq_.fetch_add(1);
    xSemaphoreGive(lock_);
  }
  return ensureTask();
}

// A request the task has not taken yet is dropped too, so oneShotPlaying() does not wait for the
// task to open a file only to close it again.
void AudioOutEsp32::stopOneShot() {
  if (xSemaphoreTake(lock_, portMAX_DELAY) == pdTRUE) {
    pendingMp3_.clear();
    pendingSong_.reset();
    mp3Pending_.store(false);
    xSemaphoreGive(lock_);
  }
  mp3Stop_.store(true);
}

// Runs on the loop, while the audio task may hold the file. A request the task has not taken yet
// is dropped; the file it took is stopped, and the loop waits until the task has closed it.
void AudioOutEsp32::release(const std::string& path) {
  bool held = false;
  if (xSemaphoreTake(lock_, portMAX_DELAY) == pdTRUE) {
    if (sound::within(pendingMp3_, path)) {
      pendingMp3_.clear();
      mp3Pending_.store(false);
    }
    held = sound::within(openMp3_, path);
    // A request still waiting ends the open file anyway; the stop flag would cut it short too.
    if (held && mp3Seq_.load() == mp3SeenSeq_) mp3Stop_.store(true);
    xSemaphoreGive(lock_);
  }
  for (int waited = 0; held && waited < kReleaseWaitMs; waited += kReleasePollMs) {
    vTaskDelay(pdMS_TO_TICKS(kReleasePollMs));
    if (xSemaphoreTake(lock_, portMAX_DELAY) != pdTRUE) continue;
    held = sound::within(openMp3_, path);
    xSemaphoreGive(lock_);
  }
  if (held) logf("MP3 %s still open after %d ms", path.c_str(), kReleaseWaitMs);
}

void AudioOutEsp32::stopStream() {
  stopRequested_.store(true);
  playing_.store(false);
}

void AudioOutEsp32::setVolumes(const sound::Volumes& volumes) {
  alertVolume_.store(volumes.alert);
  appVolume_.store(volumes.app);
  radioVolume_.store(volumes.radio);
}

void AudioOutEsp32::publishTitle(const std::string& title) {
  if (xSemaphoreTake(lock_, portMAX_DELAY) != pdTRUE) return;
  pendingTitle_ = title;
  xSemaphoreGive(lock_);
  handoffSeq_.fetch_add(1);
}

void AudioOutEsp32::publishError(const std::string& message) {
  if (xSemaphoreTake(lock_, portMAX_DELAY) != pdTRUE) return;
  pendingError_ = message;
  xSemaphoreGive(lock_);
  handoffSeq_.fetch_add(1);
}

// The audio task parks state here rather than touch the engine.
void AudioOutEsp32::tick(int64_t) {
  const uint32_t seq = handoffSeq_.load();
  if (seq == seenSeq_) return;
  seenSeq_ = seq;

  std::string title;
  std::string error;
  // Never block the main loop on the audio task's lock; rewind so the next tick retries.
  if (xSemaphoreTake(lock_, 0) != pdTRUE) {
    seenSeq_ = seq - 1;
    return;
  }
  title.swap(pendingTitle_);
  error.swap(pendingError_);
  xSemaphoreGive(lock_);

  RuntimeState& runtime = engine_.state().runtime();

  if (!error.empty()) {
    runtime.radioError = error;
    runtime.radioPlaying = false;
    engine_.state().emit(StateEvent::AudioChanged);
    return;
  }
  if (title.empty()) return;

  runtime.radioTitle = title;
  runtime.radioPlaying = playing_.load();
  engine_.state().emit(StateEvent::AudioChanged);
}

void AudioOutEsp32::taskEntry(void* self) {
  static_cast<AudioOutEsp32*>(self)->run();
}

bool AudioOutEsp32::writeDecodedFrame(const mp3::DecodeResult& result, int16_t* pcm) {
  if (result.sampleRateHz != sampleRateHz_ || result.channels != channels_) {
    closeStream();
    i2s_config_t config = {};
    config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX);
    config.sample_rate = static_cast<uint32_t>(result.sampleRateHz);
    config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    config.channel_format = result.channels == 1 ? I2S_CHANNEL_FMT_ONLY_LEFT
                                                 : I2S_CHANNEL_FMT_RIGHT_LEFT;
    config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    config.dma_buf_count = kDmaBufferCount;
    config.dma_buf_len = kDmaBufferFrames;
    config.use_apll = false;
    // Set only once the driver is up, or a failed install would let later frames write into
    // a driver that is not there.
    if (i2s_driver_install(I2S_NUM_0, &config, 0, nullptr) != ESP_OK) return false;
    i2s_pin_config_t pins = {};
    pins.bck_io_num = pinBclk_;
    pins.ws_io_num = pinLrclk_;
    pins.data_out_num = pinDout_;
    pins.data_in_num = I2S_PIN_NO_CHANGE;
    pins.mck_io_num = pinMclk_ >= 0 ? pinMclk_ : I2S_PIN_NO_CHANGE;
    i2s_set_pin(I2S_NUM_0, &pins);
    sampleRateHz_ = result.sampleRateHz;
    channels_ = result.channels;
    i2sStarted_ = true;
  }

  // Analysed before the gain, so the volume setting does not change the picture.
  audio::FrameStats stats;
  const bool analyzed =
      stats_.wanted(monotonicMs()) &&
      analyzer_.analyze(pcm, result.samples, result.channels, result.sampleRateHz, stats);

  uint8_t volume = radioVolume_.load();
  if (mp3Playing_.load())
    volume = mp3Group_ == sound::Group::App ? appVolume_.load() : alertVolume_.load();
  if (volume != scaledVolume_) {
    scaledVolume_ = volume;
    scale_ = sound::volumeGain(volume, 100);
  }
  if (scale_ < sound::kVolumeUnity) {
    const int count = result.samples * result.channels;
    for (int i = 0; i < count; ++i)
      pcm[i] = static_cast<int16_t>((static_cast<int32_t>(pcm[i]) * scale_) >> 15);
  }
  // Blocks until the DMA queue has room, which paces the whole loop to real time.
  std::size_t written = 0;
  i2s_write(I2S_NUM_0, pcm,
            static_cast<std::size_t>(result.samples) * result.channels * sizeof(int16_t),
            &written, portMAX_DELAY);
  if (analyzed)
    stats_.publish(stats, monotonicMs() + audibleLeadMs(result.samples, result.sampleRateHz));
  return true;
}

bool AudioOutEsp32::analysis(int64_t nowMs, audio::FrameStats& out) {
  stats_.markInterest(nowMs);
  return stats_.latestAudibleAt(nowMs, out);
}

namespace {
int readMp3Bytes(void* ctx, uint8_t* dst, std::size_t max) {
  return static_cast<File*>(ctx)->read(dst, max);
}
}

void AudioOutEsp32::playMp3File(const std::string& path, int16_t* pcm) {
  File file = LittleFS.open(path.c_str(), "r");
  if (!file) {
    mp3Playing_.store(false);
    logf("MP3 %s disappeared before playback", path.c_str());
    return;
  }

  decoder_.reset();
  mp3::Mp3FileDecoder walk(decoder_);
  mp3::DecodeResult result;
  const uint32_t startSeq = mp3SeenSeq_;
  bool decodeFailed = false;
  bool i2sFailed = false;
  bool finished = false;

  while (!mp3Stop_.load() && mp3Seq_.load() == startSeq) {
    const mp3::Mp3FileDecoder::Step step = walk.next(readMp3Bytes, &file, pcm, result);
    if (step == mp3::Mp3FileDecoder::Step::Done) {
      finished = true;
      break;
    }
    if (step == mp3::Mp3FileDecoder::Step::Error) {
      decodeFailed = true;
      break;
    }
    if (!writeDecodedFrame(result, pcm)) {
      i2sFailed = true;
      break;
    }
  }

  file.close();
  decoder_.reset();
  // A finished MP3 still has up to a DMA queue in flight.
  if (finished && i2sStarted_ && sampleRateHz_ > 0)
    vTaskDelay(pdMS_TO_TICKS((kDmaBufferCount * kDmaBufferFrames * 1000) / sampleRateHz_));
  // A starved I2S does not go quiet: the driver keeps clocking its descriptors, so the tail
  // would repeat for the whole reconnect.
  closeStream();
  mp3Playing_.store(false);
  if (decodeFailed) logf("MP3 %s is not audio this device can play", path.c_str());
  if (i2sFailed) logf("could not start the I2S output for MP3 %s", path.c_str());
}

void AudioOutEsp32::playSongPcm(std::shared_ptr<const synth::Song> song, int16_t* pcm) {
  std::unique_ptr<synth::Player> player(new synth::Player(kSongRate, kSongVoices, true));
  std::vector<float> block(kSongBlockFrames);
  std::vector<int32_t> sum(kSongBlockFrames);
  audio::Limiter limiter(kSongRate, kSongBlockFrames);
  player->play(std::move(song));

  mp3::DecodeResult result;
  result.status = mp3::DecodeStatus::Ok;
  result.sampleRateHz = static_cast<int>(kSongRate);
  result.channels = 1;
  result.samples = kSongBlockFrames;
  const uint32_t startSeq = mp3SeenSeq_;
  bool i2sFailed = false;
  bool finished = false;

  while (!mp3Stop_.load() && mp3Seq_.load() == startSeq) {
    if (player->idle()) {
      finished = true;
      break;
    }
    player->render(block.data(), block.size());
    for (int i = 0; i < kSongBlockFrames; ++i) sum[i] = static_cast<int32_t>(block[i] * 32767.0f);
    limiter.apply(sum.data(), pcm, kSongBlockFrames);
    if (!writeDecodedFrame(result, pcm)) {
      i2sFailed = true;
      break;
    }
  }

  player.reset();
  if (finished && i2sStarted_ && sampleRateHz_ > 0)
    vTaskDelay(pdMS_TO_TICKS((kDmaBufferCount * kDmaBufferFrames * 1000) / sampleRateHz_));
  closeStream();
  mp3Playing_.store(false);
  if (i2sFailed) logf("could not start the I2S output for a song");
}

// The audio task: core 1, priority 2, never returns. Nothing here may touch the engine.
void AudioOutEsp32::run() {
  audio::StreamInputBuffer input(radio::kInputBufferBytes);
  std::vector<int16_t> pcm(mp3::kMaxPcmPerFrame);
  std::unique_ptr<WiFiClient> plain;
  std::unique_ptr<WiFiClientSecure> secure;
  Client* client = nullptr;
  int attempt = 0;
  radio::StationUrl station;
  uint32_t stationSeq = 0;

  auto waitMs = [this](uint32_t ms) {
    for (uint32_t waited = 0; waited < ms && mp3Seq_.load() == mp3SeenSeq_; waited += 100)
      vTaskDelay(pdMS_TO_TICKS(100));
  };

  for (;;) {
    if (mp3Seq_.load() != mp3SeenSeq_) {
      std::string path;
      std::shared_ptr<const synth::Song> song;
      // Taken under the lock that release() checks, so no file is opened behind its back.
      if (xSemaphoreTake(lock_, portMAX_DELAY) == pdTRUE) {
        path = pendingMp3_;
        song = std::move(pendingSong_);
        pendingSong_.reset();
        openMp3_ = path;
        mp3Group_ = pendingGroup_;
        mp3SeenSeq_ = mp3Seq_.load();
        // Pending turns into playing under the lock, so oneShotPlaying() never drops in between.
        if (!path.empty() || song) mp3Playing_.store(true);
        mp3Pending_.store(false);
        xSemaphoreGive(lock_);
      }
      if (song)
        playSongPcm(std::move(song), pcm.data());
      else if (!path.empty())
        playMp3File(path, pcm.data());
      if (xSemaphoreTake(lock_, portMAX_DELAY) == pdTRUE) {
        openMp3_.clear();
        xSemaphoreGive(lock_);
      }
      continue;
    }

    std::string url;
    uint32_t seq = 0;
    if (xSemaphoreTake(lock_, portMAX_DELAY) == pdTRUE) {
      url = pendingUrl_;
      seq = urlSeq_.load();
      xSemaphoreGive(lock_);
    }

    // Held, the station waits out a repeating one-shot instead of reconnecting in every gap.
    if (stopRequested_.load() || url.empty() || streamHeld_.load()) {
      closeStream();
      plain.reset();
      secure.reset();
      client = nullptr;
      playing_.store(false);
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    if (seq != stationSeq) {
      station = radio::StationUrl(url);
      stationSeq = seq;
    }
    radio::Url target;
    std::string current = station.current();
    radio::ResponseHead head;
    bool connected = false;

#ifdef AWTRIX_HEAP_PROBE
    const std::size_t watchFree = heap_caps_get_free_size(kGuardHeapCaps);
    probe::watchBegin();
#endif

    for (int redirect = 0;
         redirect <= kMaxRedirects && !connected && mp3Seq_.load() == mp3SeenSeq_; ++redirect) {
      if (!radio::parseUrl(current, target)) break;

      if (target.tls) {
        secure.reset(new WiFiClientSecure());
        secure->setInsecure();
        secure->setTimeout(kConnectTimeoutMs / 1000);
        client = secure.get();
      } else {
        plain.reset(new WiFiClient());
        plain->setTimeout(kConnectTimeoutMs / 1000);
        client = plain.get();
      }

      if (!client->connect(target.host.c_str(), target.port)) break;
      const std::string request = radio::buildRequest(target, kUserAgent);
      client->write(reinterpret_cast<const uint8_t*>(request.data()), request.size());

      std::string raw;
      const uint32_t started = millis();
      while (millis() - started < radio::kReadTimeoutMs && raw.find("\r\n\r\n") == std::string::npos &&
             raw.size() < 4096) {
        if (!client->available()) {
          vTaskDelay(pdMS_TO_TICKS(10));
          continue;
        }
        raw.push_back(static_cast<char>(client->read()));
      }

      head = radio::ResponseHead{};
      if (!radio::parseResponseHead(raw, head)) break;
      if (head.status >= 300 && head.status < 400 && !head.location.empty()) {
        current = radio::resolveRedirect(target, head.location);
        client->stop();
        continue;
      }
      if (head.status != 200) break;
      connected = true;
    }

#ifdef AWTRIX_HEAP_PROBE
    {
      const probe::Watch w = probe::watchPeek();
      logf("probe radio conn %s: b %u low %u max %u ok %d", target.host.c_str(),
           static_cast<unsigned>(watchFree), static_cast<unsigned>(w.lowWater),
           static_cast<unsigned>(w.maxAlloc), connected ? 1 : 0);
    }
#endif

    if (mp3Seq_.load() != mp3SeenSeq_) {
      if (client) client->stop();
      continue;
    }

    if (!connected) {
      station.restart();
      const uint32_t wait = radio::kBackoffMs[attempt < 3 ? attempt : 2];
      if (attempt < 3) ++attempt;
      publishError("connect failed");
      waitMs(wait);
      continue;
    }

    if (radio::isPlaylistType(head.contentType)) {
      std::string body;
      const uint32_t started = millis();
      while (millis() - started < radio::kReadTimeoutMs && body.size() < radio::kMaxPlaylistBytes && client->connected()) {
        if (!client->available()) {
          vTaskDelay(pdMS_TO_TICKS(10));
          continue;
        }
        body.push_back(static_cast<char>(client->read()));
      }
      client->stop();
      if (!station.follow(body)) {
        publishError("empty playlist");
        waitMs(radio::kBackoffMs[2]);
      }
      continue;
    }

    attempt = 0;
    std::size_t bytesSeen = 0;
    bool decodedAnything = false;
    bool prerolled = false;
    uint32_t playStartMs = 0;
    int64_t deliveredSamples = 0;
    splitter_.reset(head.metaInt);
    tracker_.reset();
    decoder_.reset();
    input.clear();
    playing_.store(true);

    uint8_t chunk[kNetworkChunkBytes];
    uint32_t lastData = millis();
#ifdef AWTRIX_HEAP_PROBE
    uint32_t lastWatchMs = millis();
#endif
    while (!stopRequested_.load() && urlSeq_.load() == seq &&
           mp3Seq_.load() == mp3SeenSeq_) {
#ifdef AWTRIX_HEAP_PROBE
      if (millis() - lastWatchMs >= 30000) {
        lastWatchMs = millis();
        const probe::Watch w = probe::watchPeek();
        logf("probe radio str: f %u lg %u low %u max %u n %u sv %u",
             static_cast<unsigned>(heap_caps_get_free_size(kGuardHeapCaps)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(kGuardHeapCaps)),
             static_cast<unsigned>(w.lowWater), static_cast<unsigned>(w.maxAlloc),
             static_cast<unsigned>(w.count), static_cast<unsigned>(starvedMs_.load()));
      }
#endif
      // Compressed bytes that do not fit in the input buffer stay in the socket.
      bool received = false;
      while (input.room()) {
        const std::size_t room = input.room();
        const int available = client->available();
        if (available <= 0) break;
        int want = available > static_cast<int>(sizeof(chunk)) ? static_cast<int>(sizeof(chunk))
                                                               : available;
        if (static_cast<std::size_t>(want) > room) want = static_cast<int>(room);
        const int got = client->read(chunk, want);
        if (got <= 0) break;
        lastData = millis();
        received = true;

        bytesSeen += static_cast<std::size_t>(got);
        splitter_.feed(
            chunk, static_cast<std::size_t>(got),
            [&](const uint8_t* data, std::size_t bytes) {
              // The socket read is bounded by room; splitting out metadata can only reduce bytes.
              input.push(data, bytes);
            },
            [&](const std::string& block) {
              if (tracker_.update(block)) publishTitle(tracker_.title());
            });
      }

      if (!received) {
        if (!client->connected() || millis() - lastData > radio::kReadTimeoutMs) break;
        if (input.size() == 0) {
          starvedMs_.fetch_add(5);
          vTaskDelay(pdMS_TO_TICKS(5));
          continue;
        }
      }

      if (!prerolled) {
        if (input.size() < radio::kPrerollBytes) {
          if (!received) vTaskDelay(pdMS_TO_TICKS(5));
          continue;
        }
        prerolled = true;
      }

      const auto decode = [&](const audio::StreamInputBuffer::View& encoded) {
        const int64_t decodeStart = esp_timer_get_time();
        const mp3::DecodeResult result = decoder_.decode(encoded.data, encoded.size, pcm.data());
        if (result.status == mp3::DecodeStatus::Ok) {
          const uint32_t took = static_cast<uint32_t>(esp_timer_get_time() - decodeStart);
          const uint32_t previous = decodeUs_.load();
          decodeUs_.store(previous ? (previous * 7 + took) / 8 : took);
        }
        return result;
      };
      const auto play = [&](const mp3::DecodeResult& result) {
        if (!writeDecodedFrame(result, pcm.data())) {
          publishError("I2S failed");
          stopRequested_.store(true);
          return false;
        }

        if (playStartMs == 0) playStartMs = millis();
        deliveredSamples += result.samples;
        const int64_t deliveredMs = sampleRateHz_ > 0
                                        ? (deliveredSamples * 1000) / sampleRateHz_
                                        : 0;
        const int64_t elapsedMs = static_cast<int64_t>(millis() - playStartMs);
        const int64_t slackMs = (kDmaBufferCount * kDmaBufferFrames * 1000LL) /
                                (sampleRateHz_ > 0 ? sampleRateHz_ : 44100);
        // Wall clock ahead of the audio handed over by more than the queue holds: the speaker went
        // silent.
        if (elapsedMs - deliveredMs > slackMs) {
          underruns_.fetch_add(1);
          playStartMs = millis();
          deliveredSamples = 0;
        }
        return true;
      };
      if (audio::decodeFrames(input, kFramesPerPass, decode, play)) decodedAnything = true;
      bufferBytes_.store(static_cast<uint32_t>(input.size()));
      if (!decodedAnything && bytesSeen > radio::kUndecodableAfterBytes) {
        publishError("not playable MP3");
        stopRequested_.store(true);
      }
    }

#ifdef AWTRIX_HEAP_PROBE
    {
      const probe::Watch w = probe::watchEnd();
      logf("probe radio end: low %u max %u n %u", static_cast<unsigned>(w.lowWater),
           static_cast<unsigned>(w.maxAlloc), static_cast<unsigned>(w.count));
    }
#endif

    const bool switched = urlSeq_.load() != seq || mp3Seq_.load() != mp3SeenSeq_;
    station.restart();
    closeStream();
    if (client) client->stop();
    playing_.store(false);
    if (!stopRequested_.load() && !switched) {
      waitMs(radio::kBackoffMs[0]);
    }
  }
}

void AudioOutEsp32::closeStream() {
  if (!i2sStarted_) return;
  i2s_driver_uninstall(I2S_NUM_0);
  i2sStarted_ = false;
  sampleRateHz_ = 0;
  channels_ = 0;
}

}

#endif
