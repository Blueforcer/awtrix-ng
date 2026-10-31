#include "platform/tc002/wake/WakeListener.h"

#include <algorithm>
#include <array>
#include <climits>
#include <utility>

#include "platform/tc002/wake/ModelFiles.h"
#include "platform/tc002/wake/WakeDetector.h"
#include "system/Log.h"

namespace awtrix::tc002::wake {
namespace {
constexpr int64_t kStopMs = 2000;
constexpr std::size_t kQueueBlocks = 16;
// The frontend's noise estimate settles within about 0.4 s of a reset; measured on room noise and
// on recordings from the clock, a fresh stream reached a window mean of 0.62 before that.
constexpr uint32_t kCooloffMs = 1000, kWarmupMs = 1000;
constexpr std::array<int64_t, 5> kBackoffMs{1000, 2000, 5000, 10000, 30000};

}  // namespace

std::unique_ptr<WakeListener> WakeListener::open(const std::string& modelPath, MicrophonePort microphone,
                                                 std::string& error) {
  ModelFiles files;
  if (!readModelFiles(modelPath, files, error)) return nullptr;
  auto detector = std::make_unique<WakeDetector>();
  if (!detector->load(files.model, error)) return nullptr;
  if (files.window < 1 || files.window > 64 ||
      !detector->configure(static_cast<float>(files.cutoff), static_cast<std::size_t>(files.window), kCooloffMs,
                           kWarmupMs)) {
    error = files.settingsPath + ": cutoff must be 0 to 1, window 1 to 64";
    return nullptr;
  }
  return std::unique_ptr<WakeListener>(new WakeListener(std::move(detector), std::move(microphone)));
}

WakeListener::WakeListener(std::unique_ptr<WakeDetector> detector, MicrophonePort microphone)
    : microphone_(std::move(microphone)), detector_(std::move(detector)) {
  worker_ = std::thread([this] { run(); });
}

WakeListener::~WakeListener() {
  cancel();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    quit_ = true;
  }
  wake_.notify_all();
  worker_.join();
}

void WakeListener::send(StreamControl::Operation operation) { microphone_({epoch_, operation}); }

void WakeListener::startStream(int64_t now) {
  epoch_ = nextEpoch_;
  nextEpoch_ = nextEpoch_ == INT32_MAX ? 1 : nextEpoch_ + 1;
  state_ = State::Probing;
  stateAt_ = now;
  send(StreamControl::Probe);
}

// A probe has no stream to end, so its stop is answered by nothing.
void WakeListener::stopStream(int64_t now) {
  send(StreamControl::Stop);
  if (state_ == State::Probing) {
    epoch_ = 0;
    state_ = State::Idle;
    return;
  }
  state_ = State::Stopping;
  stopDeadline_ = now + kStopMs;
}

void WakeListener::ended(const std::string& error, int64_t now) {
  const bool wanted = state_ != State::Stopping;
  epoch_ = 0;
  state_ = State::Idle;
  stopDeadline_ = -1;
  queue({{}, true, 0});
  if (!wanted) return;
  if (error != error_) logf("wake word: microphone stream ended%s%s", error.empty() ? "" : ": ", error.c_str());
  error_ = error;
  retryAt_ = now + kBackoffMs[std::min<std::size_t>(failures_++, kBackoffMs.size() - 1)];
}

void WakeListener::cancel() {
  if (state_ == State::Probing || state_ == State::Starting || state_ == State::Listening) send(StreamControl::Stop);
  epoch_ = 0;
  state_ = State::Idle;
  stopDeadline_ = -1;
}

std::optional<WakeHeard> WakeListener::tick(int64_t now, bool listen) {
  std::vector<WakeHeard> heard;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    heard.swap(heard_);
  }
  switch (state_) {
    case State::Idle:
    case State::Unavailable:
      if (listen && now >= retryAt_) startStream(now);
      break;
    case State::Probing:
      if (!listen) stopStream(now);
      else if (now - stateAt_ >= kStopMs) ended("microphone did not answer", now);
      break;
    case State::Starting:
    case State::Listening:
      if (!listen) stopStream(now);
      else if (now - keepaliveAt_ >= kStreamKeepaliveMs) {
        keepaliveAt_ = now;
        send(StreamControl::Keepalive);
      }
      break;
    case State::Stopping:
      // The daemon's consumer lease ends the stream on its own if the end never arrives.
      if (now >= stopDeadline_) ended({}, now);
      break;
  }
  if (heard.empty() || !listen) return std::nullopt;
  logf("wake word: heard (score %.2f)", static_cast<double>(heard.back().score));
  return heard.back();
}

void WakeListener::receive(const StreamEvent& event, int64_t now) {
  if (!epoch_ || event.epoch != epoch_) return;
  switch (event.kind) {
    case StreamEvent::Kind::Support:
      if (state_ != State::Probing) return;
      if (!event.available) {
        epoch_ = 0;
        state_ = State::Unavailable;
        if (error_.empty()) logf("wake word: the microphone stream needs the MCU extension");
        error_ = "unavailable";
        retryAt_ = now + kBackoffMs.back();
        return;
      }
      state_ = State::Starting;
      keepaliveAt_ = now;
      send(StreamControl::Start);
      return;
    case StreamEvent::Kind::Started:
      if (state_ != State::Starting) return;
      state_ = State::Listening;
      expected_ = 0;
      failures_ = 0;
      error_.clear();
      queue({{}, true, 0});
      return;
    case StreamEvent::Kind::Audio:
      if (state_ != State::Listening) return;
      if (event.firstSample != expected_) queue({{}, true, event.firstSample});
      expected_ = event.firstSample + static_cast<uint32_t>(event.samples.size());
      queue({event.samples, false, event.firstSample});
      return;
    case StreamEvent::Kind::Ended:
      ended(event.error, now);
      return;
  }
}

void WakeListener::queue(Block block) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (blocks_.size() >= kQueueBlocks) {
      blocks_.clear();
      blocks_.push_back({{}, true, block.at});
    }
    blocks_.push_back(std::move(block));
  }
  wake_.notify_one();
}

void WakeListener::run() {
  std::unique_lock<std::mutex> lock(mutex_);
  for (;;) {
    wake_.wait(lock, [this] { return quit_ || !blocks_.empty(); });
    if (quit_) return;
    Block block = std::move(blocks_.front());
    blocks_.pop_front();
    lock.unlock();
    if (block.reset) {
      detector_->reset();
      lock.lock();
      base_ = block.at;
      continue;
    }
    const auto heard = detector_->feed(block.samples.data(), block.samples.size());
    lock.lock();
    if (heard) heard_.push_back({heard->score, base_ + static_cast<uint32_t>(heard->sample)});
  }
}

}  // namespace awtrix::tc002::wake
