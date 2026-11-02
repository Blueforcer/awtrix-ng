#include "platform/tc002/voice/VoiceRecorder.h"

#include <utility>

#include "platform/posix/Sequence.h"
#include "system/Log.h"

namespace awtrix::tc002::voice {
namespace {

constexpr int64_t kQuietWaitMs = 1000;

int endStatus(VoiceCapture::Outcome outcome) {
  switch (outcome) {
    case VoiceCapture::Outcome::Silence: return kSilence;
    case VoiceCapture::Outcome::NoRoom: return kFull;
    case VoiceCapture::Outcome::Recorded:
    case VoiceCapture::Outcome::Failed: return kBusy;
  }
  return kBusy;
}

}

VoiceRecorder::VoiceRecorder(ClipStore& clips, Services services)
    : clips_(clips),
      services_(std::move(services)),
      capture_(services_.microphone, services_.startFrom, services_.voiceActivityModel) {}

void VoiceRecorder::update(int64_t now) {
  if (recording_ && recording_->pending &&
      (services_.speaker->quiet() || now - recording_->pending->since >= kQuietWaitMs)) {
    const wake::EndpointSettings settings = recording_->pending->settings;
    recording_->pending.reset();
    startCapture(settings, {}, now);
  }
  capture_.tick(now);
  if (const auto result = capture_.take()) {
    if (result->outcome == VoiceCapture::Outcome::Failed || result->outcome == VoiceCapture::Outcome::NoRoom)
      logf("voice: recording failed: %s", result->error.c_str());
    if (result->outcome == VoiceCapture::Outcome::Recorded &&
        clips_.commit(recording_->clip, result->bytes, ClipKind::Wav))
      deliverRecording(result->ms);
    else
      endRecording(endStatus(result->outcome));
  }
  if (playback_ && services_.speaker->finished()) endPlayback(services_.speaker->heard());
}

int VoiceRecorder::record(const std::string& app, const wake::EndpointSettings& settings, bool fromTrigger) {
  if (!services_.visible(app) || services_.assistantBusy() || recording_ || !clips_.ready()) return kBusy;
  std::string error;
  if (!capture_.prepare(error)) {
    logf("voice: recording failed: %s", error.c_str());
    return kBusy;
  }
  const ClipId clip = clips_.promise(app, capture_.maxBytes(settings));
  if (!clip || !services_.claimVoiceChannel()) {
    if (clip) clips_.release(app, clip);
    capture_.cancel();
    return clip ? kBusy : kFull;
  }
  const MicrophoneMark mark = services_.takeMark(app);
  recordNumber_ = posix::nextInSequence(recordNumber_);
  recording_ = Recording{app, clip, recordNumber_, std::nullopt};
  if (fromTrigger && services_.live(mark))
    startCapture(settings, mark, services_.clock());
  else
    recording_->pending = PendingCapture{settings, services_.clock()};
  return recordNumber_;
}

void VoiceRecorder::startCapture(const wake::EndpointSettings& settings, MicrophoneMark from, int64_t now) {
  if (!capture_.start(clips_.path(recording_->clip), settings, from, now)) endRecording(kBusy);
}

void VoiceRecorder::deliverRecording(uint32_t ms) {
  services_.emit(VoiceEvent::ofClip(recording_->app, VoiceEvent::Kind::Record, recording_->number, recording_->clip,
                                    ms, clipFormat(ClipKind::Wav).extension));
  clearRecording();
}

void VoiceRecorder::endRecording(int status) {
  clips_.release(recording_->app, recording_->clip);
  services_.emit(VoiceEvent::outcome(recording_->app, VoiceEvent::Kind::Record, recording_->number, status));
  clearRecording();
}

void VoiceRecorder::clearRecording() {
  capture_.cancel();
  recording_.reset();
}

void VoiceRecorder::stop(const std::string& app) {
  if (!records(app)) return;
  if (recording_->pending)
    endRecording(kSilence);
  else
    capture_.stop();
}

void VoiceRecorder::end(const std::string& app) {
  if (records(app)) endRecording(kCancelled);
  if (plays(app)) stopPlayback();
}

int VoiceRecorder::play(const std::string& app, ClipId id) {
  const Clip* clip = clips_.find(app, id);
  if (!clip || services_.assistantBusy() || !services_.speaker->available()) return 0;
  if (playback_) stopPlayback();
  if (!services_.claimVoiceChannel() || !services_.speaker->play(clip->path)) return 0;
  playNumber_ = posix::nextInSequence(playNumber_);
  playback_ = Playback{app, playNumber_};
  return playNumber_;
}

void VoiceRecorder::stopPlayback() {
  services_.speaker->stop();
  endPlayback(false);
}

void VoiceRecorder::endPlayback(bool heard) {
  services_.emit(VoiceEvent::outcome(playback_->app, VoiceEvent::Kind::Play, playback_->number, heard ? 1 : 0));
  playback_.reset();
}

const char* VoiceRecorder::state(const std::string& app) const {
  if (records(app)) return "recording";
  if (plays(app)) return "playing";
  return "idle";
}

}
