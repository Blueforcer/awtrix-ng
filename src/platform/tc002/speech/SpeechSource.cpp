#include "platform/tc002/speech/SpeechSource.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "platform/tc002/speech/SpeechLimiter.h"
#include "platform/tc002/speech/SpeechRing.h"

namespace awtrix::speech {

struct SpeechSource::Shared {
  Shared(std::shared_ptr<SpeechVoice> voice, std::shared_ptr<const Plan> plan, std::size_t capacity)
      : voice(std::move(voice)), plan(std::move(plan)), ring(capacity) {}

  const std::shared_ptr<SpeechVoice> voice;
  const std::shared_ptr<const Plan> plan;
  std::mutex mutex;
  std::condition_variable room;
  SpeechRing ring;
  // Set while the worker renders, so a cancel reaches the voice.
  SpeechUtterance* utterance = nullptr;
  bool cancelled = false;
  bool done = false;
  bool failed = false;
};

namespace {

using Shared = SpeechSource::Shared;

std::mutex g_workersMutex;
std::condition_variable g_workersDone;
int g_workers = 0;

// Renders outside the lock and takes it only to hand a block over, or to wait for room. A voice
// that throws has failed; the runtime goes on.
void work(Shared& shared) {
  std::unique_ptr<SpeechUtterance> utterance;
  bool failed = false;
  try {
    utterance = shared.voice->start(*shared.plan);
    failed = !utterance;
    bool cancelled;
    {
      std::lock_guard<std::mutex> lock(shared.mutex);
      cancelled = shared.cancelled;
      if (!cancelled) shared.utterance = utterance.get();
    }
    SpeechLimiter limiter(shared.voice->rate());
    int16_t block[SpeechSource::kBlockFrames];
    while (utterance && !cancelled) {
      std::size_t frames = utterance->render(block, SpeechSource::kBlockFrames);
      const bool end = frames == 0;
      if (end)
        frames = limiter.flush(block, SpeechSource::kBlockFrames);
      else
        limiter.process(block, frames);
      if (frames) {
        std::unique_lock<std::mutex> lock(shared.mutex);
        shared.room.wait(lock, [&] { return shared.cancelled || shared.ring.room() >= frames; });
        cancelled = shared.cancelled;
        if (!cancelled) shared.ring.write(block, frames);
      }
      if (end) break;
    }
  } catch (...) {
    failed = true;
  }
  std::lock_guard<std::mutex> lock(shared.mutex);
  shared.utterance = nullptr;
  shared.done = true;
  shared.failed = failed;
}

void leave() {
  {
    std::lock_guard<std::mutex> lock(g_workersMutex);
    --g_workers;
  }
  g_workersDone.notify_all();
}

}

std::unique_ptr<SpeechSource> SpeechSource::open(std::shared_ptr<SpeechVoice> voice,
                                                 std::shared_ptr<const Plan> plan) {
  const uint32_t rate = voice->rate();
  const std::size_t capacity = std::max<std::size_t>(kBlockFrames, std::size_t{rate} * kBufferMs / 1000);
  auto shared = std::make_shared<Shared>(std::move(voice), std::move(plan), capacity);
  {
    std::lock_guard<std::mutex> lock(g_workersMutex);
    ++g_workers;
  }
  try {
    std::thread([shared]() mutable {
      work(*shared);
      shared.reset();
      leave();
    }).detach();
  } catch (...) {
    leave();
    return nullptr;
  }
  return std::unique_ptr<SpeechSource>(new SpeechSource(std::move(shared), rate));
}

SpeechSource::SpeechSource(std::shared_ptr<Shared> shared, uint32_t rate)
    : shared_(std::move(shared)), rate_(rate), preroll_(std::size_t{rate} * kPrerollMs / 1000) {}

SpeechSource::~SpeechSource() {
  std::lock_guard<std::mutex> lock(shared_->mutex);
  shared_->cancelled = true;
  if (shared_->utterance) shared_->utterance->cancel();
  shared_->room.notify_all();
}

tc002::PcmSource::Read SpeechSource::next(const int16_t*& samples, std::size_t& frames) {
  std::lock_guard<std::mutex> lock(shared_->mutex);
  if (!started_) started_ = shared_->done || shared_->ring.size() >= preroll_;
  if (!started_) return Read::Wait;
  frames = shared_->ring.read(block_, kBlockFrames);
  if (frames) {
    shared_->room.notify_one();
    samples = block_;
    return Read::Data;
  }
  if (!shared_->done) return Read::Wait;
  return shared_->failed ? Read::Error : Read::End;
}

bool SpeechSource::waitForWorkers(int timeoutMs) {
  std::unique_lock<std::mutex> lock(g_workersMutex);
  return g_workersDone.wait_for(lock, std::chrono::milliseconds(timeoutMs), [] { return g_workers == 0; });
}

}
