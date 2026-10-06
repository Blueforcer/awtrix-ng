#pragma once

#include "core/audio/AnalysisSource.h"
#include "core/audio/AnalysisEnvelope.h"
#include "core/audio/Pcm.h"
#include "core/audio/SpectrumAnalyzer.h"

namespace awtrix::audio {

// Hardware-independent analysis/cache, owned by the same thread as its consumer.
class PcmAnalysis final : public IAnalysisSource {
 public:
  explicit PcmAnalysis(SpectrumAnalyzer::Scaling scaling = SpectrumAnalyzer::Scaling::Adaptive)
      : analyzer_(scaling) {}
  bool submit(const PcmView& pcm, int64_t nowMs) {
    // Adaptive gain may span nearby independent windows; FFT samples never do.
    // Reset analysis history after an idle interval, an error or a source change.
    if (receivedAt_ < 0 || nowMs < receivedAt_ || nowMs - receivedAt_ > 1000) analyzer_.reset();
    if (!analyzer_.analyze(pcm.samples, pcm.frames, pcm.channels, pcm.sampleRate, stats_)) {
      deactivate();
      return false;
    }
    receivedAt_ = nowMs;
    return true;
  }
  bool analysis(int64_t nowMs, FrameStats& out) override {
    if (receivedAt_ < 0 || nowMs < receivedAt_ || nowMs - receivedAt_ > 500) { out = {}; return false; }
    out = envelope_.process(stats_, nowMs);
    stats_.beat = false; // A cached window can trigger a beat only once.
    return true;
  }
  void deactivate() override { receivedAt_ = -1; stats_ = {}; analyzer_.reset(); envelope_.reset(); }

 private:
  SpectrumAnalyzer analyzer_;
  AnalysisEnvelope envelope_;
  FrameStats stats_;
  int64_t receivedAt_ = -1;
};

}
