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
class MicrophoneInput final : public audio::IListeningSource {
 public:
  using Send = std::function<bool(std::string)>;
  explicit MicrophoneInput(Send send) : send_(std::move(send)) {}

  bool analysis(int64_t nowMs, audio::FrameStats& out) override {
    wanted_ = true;
    request(nowMs);
    return heard(nowMs, out);
  }

  bool heard(int64_t nowMs, audio::FrameStats& out) override { return cache_.analysis(nowMs, out); }

  // The fundamental of the newest window in Hz, or 0. It shares the windows with analysis()
  // but not its switch: deactivate() never stops it, musicPitch() decides when it is asked.
  // Computed at most once per window, and only when asked. Without listen it asks for no new
  // window and answers from the newest one.
  float pitch(int64_t nowMs, bool listen) {
    pitchAskedAt_ = nowMs;
    if (listen) request(nowMs);
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
    take(pcm.samples.data(), wanted_ && acceptAnalysis_, acceptPitch_, nowMs);
  }

  void deactivate() override { wanted_ = false; acceptAnalysis_ = false; cache_.deactivate(); }

  // True while the microphone hub has the upstream stream; the stream then feeds analysis and pitch
  // through streamed(), and no snapshot is asked for.
  void setStreaming(bool active) {
    if (active && !streaming_) { acceptAnalysis_ = acceptPitch_ = false; windowAt_ = -1; gathered_ = 0; }
    streaming_ = active;
  }

  // Blocks of the hub's stream, gathered into snapshot-sized windows for analysis and pitch. A gap in
  // the stream starts a new window. The stream runs for the hub's consumers, so apps hear it only
  // shortly after a reading that may listen, as they would hear a snapshot.
  void streamed(const int16_t* samples, std::size_t count, uint32_t firstSample, int64_t nowMs) {
    if (!streaming_ || listenedAt_ < 0 || nowMs - listenedAt_ > kListenMs) return;
    if (firstSample != nextSample_) gathered_ = 0;
    nextSample_ = firstSample + static_cast<uint32_t>(count);
    for (std::size_t i = 0; i < count; ++i) {
      gather_[gathered_++] = samples[i];
      if (gathered_ < gather_.size()) continue;
      gathered_ = 0;
      take(gather_.data(), wanted_, true, nowMs);
    }
  }

 private:
  static constexpr int64_t kPitchMaxAgeMs = 300;
  // Windows are kept for pitch only while a script has asked for it this recently.
  static constexpr int64_t kPitchDemandMs = 1000;
  // About one snapshot interval plus its capture: how long after the last reading that may listen
  // the stream still reaches apps.
  static constexpr int64_t kListenMs = 200;

  // One window of kMicrophonePcmSamples, from a snapshot or the stream: to the analysis, and kept for
  // pitch while a script asks for it.
  void take(const int16_t* samples, bool analysis, bool pitch, int64_t nowMs) {
    if (analysis)
      cache_.submit({samples, static_cast<int>(kMicrophonePcmSamples), 1, static_cast<int>(kMicrophonePcmRate)},
                    nowMs);
    if (pitch && pitchAskedAt_ >= 0 && nowMs - pitchAskedAt_ <= kPitchDemandMs) {
      std::copy(samples, samples + kMicrophonePcmSamples, window_.begin());
      windowAt_ = nowMs;
      pitchDone_ = false;
    }
  }

  void request(int64_t nowMs) {
    listenedAt_ = nowMs;
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
  std::array<int16_t, kMicrophonePcmSamples> window_{}, gather_{};
  std::size_t gathered_ = 0;
  uint32_t nextSample_ = 0;
  bool pending_ = false, wanted_ = false, acceptAnalysis_ = false, acceptPitch_ = false;
  bool streaming_ = false;
  bool pitchDone_ = false;
  float pitch_ = 0.f;
  int id_ = 0;
  int64_t sentAt_ = -1;
  int64_t windowAt_ = -1;
  int64_t pitchAskedAt_ = -1;
  int64_t listenedAt_ = -1;
};

}
