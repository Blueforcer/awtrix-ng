#include "platform/tc002/wake/WakeListener.h"

#include <algorithm>
#include <array>
#include <utility>

#include "platform/tc002/contract/SupervisorProtocol.h"
#include "platform/tc002/wake/ModelFiles.h"
#include "platform/tc002/wake/WakeDetector.h"
#include "system/Log.h"

namespace awtrix::tc002::wake {
namespace {
static_assert(kMicrophonePcmRate == WakeFrontend::kSampleRate, "the detector takes the microphone's PCM as it comes");
constexpr uint32_t kCooloffMs = 1000, kWarmupMs = 1000;
constexpr std::array<int64_t, 5> kBackoffMs{1000, 2000, 5000, 10000, 30000};

}

std::unique_ptr<WakeListener> WakeListener::open(const std::string& modelPath, MicrophonePort microphone,
                                                 std::unique_ptr<posix::Executor> worker, std::string& error) {
  ModelFiles files;
  if (!readModelFiles(modelPath, files, error)) return nullptr;
  auto detector = std::make_unique<WakeDetector>();
  if (!detector->load(files.model, error)) return nullptr;
  if (!detector->configure({static_cast<float>(files.cutoff), files.window, kCooloffMs, kWarmupMs}, error)) {
    error = files.settingsPath + ": " + error;
    return nullptr;
  }
  return std::unique_ptr<WakeListener>(new WakeListener(std::move(detector), std::move(microphone), std::move(worker)));
}

WakeListener::WakeListener(std::unique_ptr<WakeDetector> detector, MicrophonePort microphone,
                           std::unique_ptr<posix::Executor> worker)
    : microphone_(std::move(microphone)), detector_(std::move(detector)), worker_(std::move(worker)) {}

WakeListener::~WakeListener() {
  cancel();
  worker_.reset();
}

void WakeListener::cancel() { microphone_.cancel(); }

std::optional<WakeHeard> WakeListener::tick(int64_t now, Listen listen) {
  std::vector<WakeHeard> heard;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    heard.swap(heard_);
  }
  muted_ = listen != Listen::On;
  if (microphone_.phase() == MicrophoneClient::Phase::Idle) {
    if (listen != Listen::Off && now >= retryAt_) microphone_.open(now);
  } else if (listen == Listen::Off) {
    microphone_.stop(now);
  }
  apply(microphone_.tick(now), now);
  if (heard.empty() || muted_) return std::nullopt;
  logf("wake word: heard (score %.2f)", static_cast<double>(heard.back().score));
  return heard.back();
}

void WakeListener::receive(const StreamEvent& event, int64_t now) {
  const MicrophoneClient::Report kind = microphone_.receive(event, now);
  switch (kind) {
    case MicrophoneClient::Report::Gap:
      restart(event.firstSample);
      [[fallthrough]];
    case MicrophoneClient::Report::Audio:
      return queue({event.samples, false, event.firstSample, muted_});
    case MicrophoneClient::Report::None:
    case MicrophoneClient::Report::Unavailable:
    case MicrophoneClient::Report::Started:
    case MicrophoneClient::Report::Ended:
    case MicrophoneClient::Report::Stopped:
      return apply(kind, now);
  }
}

void WakeListener::apply(MicrophoneClient::Report report, int64_t now) {
  switch (report) {
    case MicrophoneClient::Report::Unavailable:
      return unavailable(now);
    case MicrophoneClient::Report::Started:
      failures_ = 0;
      error_.clear();
      return restart(0);
    case MicrophoneClient::Report::Ended:
      return failed(now);
    case MicrophoneClient::Report::Stopped:
      return restart(0);
    case MicrophoneClient::Report::None:
    case MicrophoneClient::Report::Audio:
    case MicrophoneClient::Report::Gap:
      return;
  }
}

void WakeListener::unavailable(int64_t now) {
  if (error_.empty()) logf("wake word: %s", kStreamUnavailable);
  error_ = "unavailable";
  retryAt_ = now + kBackoffMs.back();
}

void WakeListener::failed(int64_t now) {
  restart(0);
  const std::string& error = microphone_.error();
  if (error != error_) logf("wake word: microphone stream ended%s%s", error.empty() ? "" : ": ", error.c_str());
  error_ = error;
  retryAt_ = now + kBackoffMs[std::min<std::size_t>(failures_++, kBackoffMs.size() - 1)];
}

void WakeListener::restart(uint32_t origin) { queue({{}, true, origin}); }

void WakeListener::queue(Block block) {
  bool idle = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (blocks_.size() >= kQueueBlocks) {
      blocks_.clear();
      blocks_.push_back({{}, true, block.firstSample});
    }
    blocks_.push_back(std::move(block));
    idle = !std::exchange(draining_, true);
  }
  if (idle) worker_->post([this] { drain(); });
}

void WakeListener::drain() {
  std::unique_lock<std::mutex> lock(mutex_);
  while (!blocks_.empty()) {
    Block block = std::move(blocks_.front());
    blocks_.pop_front();
    lock.unlock();
    if (block.reset) {
      detector_->reset();
      detectorOrigin_ = block.firstSample;
      lock.lock();
      continue;
    }
    const auto heard = detector_->feed(block.samples.data(), block.samples.size());
    lock.lock();
    if (heard && !block.muted) heard_.push_back({heard->score, detectorOrigin_ + static_cast<uint32_t>(heard->sample)});
  }
  draining_ = false;
}

}
