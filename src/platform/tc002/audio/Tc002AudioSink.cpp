#include "platform/tc002/audio/Tc002AudioSink.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <algorithm>

#include "platform/linux/LinuxMemory.h"
#include "platform/tc002/audio/Tc002AudioStream.h"
#include "platform/tc002/contract/AudioProtocol.h"
#include "platform/tc002/speech/SpeechSource.h"
#include "system/Log.h"
#include "system/MonotonicClock.h"

namespace awtrix {
namespace tc002 {
namespace {

constexpr int kShutdownWaitMs = 1000;

}

Tc002AudioSink::Tc002AudioSink(CoreEngine& engine, int helperFd, std::shared_ptr<speech::SpeechVoice> voice)
    : voice_(std::move(voice)), engine_(engine), link_(helperFd), player_(link_),
      urls_("/tmp", urlSpeaker(), [](uint64_t& bytes) { return readMemAvailable(bytes); }) {
  if (link_.failed() || ::pipe2(wake_, O_NONBLOCK | O_CLOEXEC) != 0) {
    available_.store(false);
    return;
  }
  try {
    thread_ = std::thread([this] { run(); });
  } catch (...) {
    available_.store(false);
  }
}

Tc002AudioSink::~Tc002AudioSink() {
  quit_.store(true);
  wake();
  if (thread_.joinable()) thread_.join();
  for (int fd : wake_)
    if (fd >= 0) ::close(fd);
  StreamSource::waitForNetworkThreads(kShutdownWaitMs);
  speech::SpeechSource::waitForWorkers(kShutdownWaitMs);
}

void Tc002AudioSink::wake() {
  if (wake_[1] < 0) return;
  const char byte = 1;
  while (::write(wake_[1], &byte, 1) < 0 && errno == EINTR) {
  }
}

void Tc002AudioSink::setVolumes(const sound::Volumes& volumes) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    request_.volumes = volumes;
  }
  wake();
}

sound::PcmPlay Tc002AudioSink::playSpec(const sound::PcmRequest& request, DispatchDetail& detail) {
  return routing_.play(*this, request, detail);
}

bool Tc002AudioSink::checkSpec(const sound::Spec& spec, DispatchDetail& detail) {
  return sound::PcmRouting::check(*this, spec, detail);
}

void Tc002AudioSink::stopLayers(sound::Stop what, const std::string& owner) {
  routing_.stop(*this, what, owner);
}

sound::PcmState Tc002AudioSink::state() {
  sound::PcmState result;
  result.oneShot = urls_.pending(false);
  result.music = urls_.pending(true);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    result.oneShot = result.oneShot || (request_.oneShot != Kind::None && request_.oneShot != Kind::Tone);
    result.groupKnown = request_.oneShot != Kind::None;
    result.group = request_.oneShotGroup;
    result.effects = effectsPending_ > 0 || effectVoices_.load() > 0;
    result.music = result.music || !request_.loop.empty();
  }
  routing_.observe(result);
  return result;
}

bool Tc002AudioSink::takeError(sound::PcmError& error) {
  bool music = false;
  if (!takeUrlError(error.message, music)) return false;
  std::lock_guard<std::mutex> lock(mutex_);
  error.group = music ? sound::Group::App : urlGroup_;
  error.stopRepeat = !music;
  return true;
}

bool Tc002AudioSink::analysis(int64_t nowMs, audio::FrameStats& out) {
  stats_.markInterest(nowMs);
  return stats_.latestAudibleAt(nowMs, out);
}

// Render thread: the audio thread parks its news in report_ and never touches the engine.
void Tc002AudioSink::tick(int64_t nowMs) {
  urls_.tick(nowMs);
  Report news;
  {
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock() || report_.seq == seenReport_) return;
    seenReport_ = report_.seq;
    news = std::move(report_);
    if (news.streamSeq != request_.streamSeq) news.errorNew = news.titleNew = false;
    report_ = Report();
    report_.seq = seenReport_;
  }
  RuntimeState& runtime = engine_.state().runtime();
  if (news.errorNew) {
    runtime.radioError = news.error;
    runtime.radioPlaying = news.error.empty() && news.streaming;
    engine_.state().emit(StateEvent::AudioChanged);
    if (!news.error.empty()) return;
  }
  if (!news.titleNew) return;
  runtime.radioTitle = news.title;
  runtime.radioPlaying = news.streaming;
  engine_.state().emit(StateEvent::AudioChanged);
}

void Tc002AudioSink::publishError(const std::string& message) {
  const bool streaming = stream_ && stream_->connected();
  std::lock_guard<std::mutex> lock(mutex_);
  if (request_.streamSeq != playingStreamSeq_) return;
  report_.streamSeq = playingStreamSeq_;
  report_.error = message;
  report_.errorNew = true;
  report_.streaming = streaming;
  ++report_.seq;
}

void Tc002AudioSink::publishTitle(const std::string& title) {
  const bool streaming = stream_ && stream_->connected();
  std::lock_guard<std::mutex> lock(mutex_);
  if (request_.streamSeq != playingStreamSeq_) return;
  report_.streamSeq = playingStreamSeq_;
  report_.title = title;
  report_.titleNew = true;
  report_.streaming = streaming;
  ++report_.seq;
}

void Tc002AudioSink::run() {
  while (!quit_.load()) {
    pollfd fds[2] = {{wake_[0], POLLIN, 0}, {link_.fd(), POLLIN, 0}};
    if (link_.wantsWrite()) fds[1].events |= POLLOUT;
    const nfds_t count = link_.failed() ? 1 : 2;
    const int timeout = again_ ? 0 : available_.load() ? player_.waitMs() : -1;
    again_ = false;
    ::poll(fds, count, timeout);
    char drain[64];
    while (::read(wake_[0], drain, sizeof drain) > 0) {
    }
    if (quit_.load()) break;
    const int64_t now = monotonicMs();
    if (available_.load()) reconcile(now);
    player_.pump(now);
    afterPump();
    voiceQuiet_.store(voiceOwned_.load() && playing_ == Playing::None &&
      link_.status().state == TC002_AUDIO_IDLE && (!link_.generation() || link_.finished(link_.generation())));
  }
  player_.stop();
  player_.takeOutcome();
  stream_ = nullptr;
  mixer_ = nullptr;
}

}
}
