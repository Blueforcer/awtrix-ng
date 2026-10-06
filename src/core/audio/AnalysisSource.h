#pragma once

#include "core/audio/AudioStats.h"

namespace awtrix::audio {

// Render-thread interface shared by playback and capture backends. Calling analysis
// expresses demand; implementations may acquire asynchronously and return false meanwhile.
class IAnalysisSource {
 public:
  virtual ~IAnalysisSource() = default;
  virtual bool analysis(int64_t nowMs, FrameStats& out) = 0;
  virtual void deactivate() {}
};

enum class AnalysisSource { Automatic, Playback, Microphone };

class AnalysisRouter {
 public:
  AnalysisRouter(IAnalysisSource* playback, IAnalysisSource* microphone)
      : playback_(playback), microphone_(microphone) {}

  bool analysis(AnalysisSource source, int64_t nowMs, FrameStats& out) {
    if (source != AnalysisSource::Microphone && playback_ && playback_->analysis(nowMs, out)) {
      if (microphone_) microphone_->deactivate();
      return true;
    }
    if (source != AnalysisSource::Playback && microphone_) return microphone_->analysis(nowMs, out);
    if (microphone_) microphone_->deactivate();
    out = {};
    return false;
  }

 private:
  IAnalysisSource* playback_;
  IAnalysisSource* microphone_;
};

}
