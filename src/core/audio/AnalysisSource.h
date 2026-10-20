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

// A source that has to be asked before it hears anything, such as a microphone. heard() answers
// from what it already has and never asks for more.
class IListeningSource : public IAnalysisSource {
 public:
  virtual bool heard(int64_t nowMs, FrameStats& out) = 0;
};

enum class AnalysisSource { Automatic, Playback, Microphone };

class AnalysisRouter {
 public:
  AnalysisRouter(IAnalysisSource* playback, IListeningSource* microphone)
      : playback_(playback), microphone_(microphone) {}

  // listen: whether this reading may make the microphone listen. Without it the microphone only
  // answers what it already heard.
  bool analysis(AnalysisSource source, int64_t nowMs, FrameStats& out, bool listen) {
    if (source != AnalysisSource::Microphone && playback_ && playback_->analysis(nowMs, out)) {
      if (microphone_) microphone_->deactivate();
      return true;
    }
    if (source != AnalysisSource::Playback && microphone_)
      return listen ? microphone_->analysis(nowMs, out) : microphone_->heard(nowMs, out);
    if (microphone_) microphone_->deactivate();
    out = {};
    return false;
  }

 private:
  IAnalysisSource* playback_;
  IListeningSource* microphone_;
};

}
