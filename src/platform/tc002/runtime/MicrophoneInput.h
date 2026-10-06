#pragma once

#include <algorithm>
#include <array>
#include <climits>
#include <functional>

#include "core/audio/PcmAnalysis.h"
#include "core/audio/PitchDetector.h"
#include "platform/tc002/contract/SupervisorProtocol.h"

namespace awtrix::tc002 {

// Adapter for the supervisor's bounded PCM input. Acquires only on demand, keeps
// at most one request in flight, and never exposes incomplete UART captures.
class MicrophoneInput final : public audio::IAnalysisSource {
 public:
  using Send = std::function<bool(std::string)>;
  explicit MicrophoneInput(Send send) : send_(std::move(send)) {}

  bool analysis(int64_t nowMs, audio::FrameStats& out) override {
    wanted_ = true;
    request(nowMs);
    return cache_.analysis(nowMs, out);
  }

  // The fundamental of the newest window in Hz, or 0. It shares the snapshots with analysis()
  // but not its switch: deactivate() never stops it, musicPitch() decides when it is asked.
  // Computed at most once per window, and only when asked.
  float pitch(int64_t nowMs) {
    pitchAskedAt_ = nowMs;
    if (streaming_) return 0.f;
    request(nowMs);
    if (windowAt_ < 0 || nowMs < windowAt_ || nowMs - windowAt_ > kPitchMaxAgeMs) return 0.f;
    if (!pitchDone_) {
      pitch_ = detector_.detect(window_.data(), static_cast<int>(window_.size()), kMicrophonePcmRate);
      pitchDone_ = true;
    }
    return pitch_;
  }

  void receive(const MicrophonePcm& pcm, int64_t nowMs) {
    if (!pending_ || pcm.id != id_) return;
    pending_ = false;
    // A power query can temporarily own the UART. Keep the previous valid window
    // until its normal expiry; never feed failed or incomplete data into the FFT.
    if (!pcm.error.empty() || pcm.samples.size() != kMicrophonePcmSamples) return;
    if (wanted_ && acceptAnalysis_)
      cache_.submit({pcm.samples.data(), static_cast<int>(pcm.samples.size()), 1,
                     static_cast<int>(kMicrophonePcmRate)}, nowMs);
    if (acceptPitch_ && pitchAskedAt_ >= 0 && nowMs - pitchAskedAt_ <= kPitchDemandMs) {
      std::copy(pcm.samples.begin(), pcm.samples.end(), window_.begin());
      windowAt_ = nowMs;
      pitchDone_ = false;
    }
  }

  void deactivate() override { wanted_ = false; acceptAnalysis_ = false; cache_.deactivate(); }
  void setStreaming(bool active) {
    streaming_ = active;
    if (active) { acceptAnalysis_ = acceptPitch_ = false; windowAt_ = -1; }
  }

 private:
  static constexpr int64_t kPitchMaxAgeMs = 300;
  // Windows are kept for pitch only while a script has asked for it this recently.
  static constexpr int64_t kPitchDemandMs = 1000;

  void request(int64_t nowMs) {
    if (pending_ && nowMs - sentAt_ >= 5500) { pending_ = false; cache_.deactivate(); }
    if (!streaming_ && !pending_ && (sentAt_ < 0 || nowMs - sentAt_ >= 100)) {
      id_ = id_ == INT_MAX ? 1 : id_ + 1;
      sentAt_ = nowMs;
      pending_ = send_(encodeMicrophonePcmRequest(id_));
      acceptAnalysis_ = acceptPitch_ = pending_;
    }
  }

  Send send_;
  audio::PcmAnalysis cache_{audio::SpectrumAnalyzer::Scaling::Fixed};
  audio::PitchDetector detector_;
  std::array<int16_t, kMicrophonePcmSamples> window_{};
  bool pending_ = false, wanted_ = false, acceptAnalysis_ = false, acceptPitch_ = false;
  bool streaming_ = false;
  bool pitchDone_ = false;
  float pitch_ = 0.f;
  int id_ = 0;
  int64_t sentAt_ = -1;
  int64_t windowAt_ = -1;
  int64_t pitchAskedAt_ = -1;
};

}
