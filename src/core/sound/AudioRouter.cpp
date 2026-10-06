#include "core/sound/AudioRouter.h"

#include "core/radio/IcyStream.h"
#include "core/sound/SoundMp3.h"

namespace awtrix {
namespace sound {
namespace {

const char* kNoOutput = "no sound output";
const char* kNoMelody = "no melody output";
const char* kNoTrackSink = "no DFPlayer";
const char* kNoPcm = "no MP3 output";
const char* kNoSynth = "no synthesizer";
const char* kNoSpeech = "no text-to-speech";
const char* kNoUrls = "no URL playback";
const char* kSpeakerBusy = "speaker unavailable";
const char* kNothingPlayable = "nothing playable";

PlayResult unavailable(DispatchDetail& detail, const char* message) {
  detail.message = message;
  return PlayResult::NoSink;
}

PlayResult mistake(DispatchDetail& detail, const char* field, const char* message) {
  detail.field = field;
  detail.message = message;
  return PlayResult::Invalid;
}

// what "value", in one buffer.
std::string quoted(const char* what, const std::string& value) {
  std::string out(what);
  out += " \"";
  out += value;
  out += '"';
  return out;
}

}

void AudioRouter::setTone(IToneSink* tone) {
  tone_ = tone;
  if (tone_ && volumesPushed_) tone_->setVolumes(volumes_);
}

void AudioRouter::setTrack(ITrackSink* track) {
  track_ = track;
  if (track_ && volumesPushed_) track_->setVolumes(volumes_);
}

void AudioRouter::setPcm(IPcmSink* pcm) {
  pcm_ = pcm;
  if (pcm_ && volumesPushed_) pcm_->setVolumes(volumes_);
}

// No-ops are dropped: a DFPlayer takes ten bytes at 9600 baud per change.
void AudioRouter::setVolumes(int master, int radio, int app, int alert) {
  const Volumes next = volumesFor(master, radio, app, alert);
  if (volumesPushed_ && next == volumes_) return;
  volumes_ = next;
  volumesPushed_ = true;
  if (tone_) tone_->setVolumes(volumes_);
  if (track_) track_->setVolumes(volumes_);
  if (pcm_) pcm_->setVolumes(volumes_);
}

PlayResult AudioRouter::play(const Choices& choices, Group group, const std::string& script,
                             DispatchDetail& detail) {
  return startFirst(choices, group, false, script, detail);
}

PlayResult AudioRouter::playEffect(const Choices& choices, const std::string& script,
                                   DispatchDetail& detail) {
  return startFirst(choices, Group::App, true, script, detail);
}

// The first entry that plays wins. A mistake in one entry is the answer at once; a list where
// nothing plays says so as a whole.
PlayResult AudioRouter::startFirst(const Choices& choices, Group group, bool effect,
                                   const std::string& script, DispatchDetail& detail) {
  PlayResult last = PlayResult::NoSink;
  for (const Spec& spec : choices) {
    detail.clear();
    last = start(spec, group, effect, script, detail);
    if (last == PlayResult::Ok || last == PlayResult::Invalid) return last;
  }
  if (choices.count > 1) {
    detail.clear();
    return unavailable(detail, kNothingPlayable);
  }
  return last;
}

// The output an entry needs; nullptr when this clock has it. Files are not looked up here.
const char* AudioRouter::missingOutput(const Spec& spec) const {
  switch (spec.kind) {
    case Kind::File:
      if (isUrl(spec.text)) return pcm_ && pcm_->caps().url ? nullptr : kNoUrls;
      if (spec.text.find('/') != std::string::npos) return pcm_ ? nullptr : kNoPcm;
      return pcm_ || tone_ ? nullptr : kNoOutput;
    case Kind::Rtttl:
      return tone_ ? nullptr : kNoMelody;
    case Kind::Song:
      return pcm_ && pcm_->caps().song ? nullptr : kNoSynth;
    case Kind::Speech:
      return pcm_ && pcm_->caps().speech ? nullptr : kNoSpeech;
    case Kind::Track:
      return track_ ? nullptr : kNoTrackSink;
    case Kind::Station:
      return pcm_ ? nullptr : kNoOutput;
  }
  return kNoOutput;
}

// What needs no hardware is judged first, so a mistake is one on every clock.
PlayResult AudioRouter::start(const Spec& spec, Group group, bool effect,
                              const std::string& script, DispatchDetail& detail) {
  radio::Url parsed;
  if (spec.kind == Kind::Station) return mistake(detail, "station", "not here");
  if (spec.kind == Kind::File && isUrl(spec.text) && !radio::parseUrl(spec.text, parsed))
    return mistake(detail, "file", "invalid URL");
  if (const char* missing = missingOutput(spec)) return unavailable(detail, missing);
  switch (spec.kind) {
    case Kind::File:
      return startFile(spec, group, effect, script, detail);
    case Kind::Song:
      return startPcm(spec, "", group, effect, script, detail);
    case Kind::Rtttl:
      if (!mayStartOneShot(group)) return PlayResult::Ok;
      if (!tone_->playRtttl(spec.text, group)) break;
      oneShotStarted(Sink::Tone, group, script, spec, effect);
      return PlayResult::Ok;
    case Kind::Speech:
      return startPcm(spec, "", group, effect, script, detail);
    case Kind::Track:
      if (!mayStartOneShot(group)) return PlayResult::Ok;
      if (!track_->playTrack(spec.number, group))
        return mistake(detail, "track", "must be 1..2999");
      oneShotStarted(Sink::Track, group, script, spec, effect);
      return PlayResult::Ok;
    case Kind::Station:
      break;
  }
  return unavailable(detail, kSpeakerBusy);
}

// An address is fetched, "Script/name" is that folder alone, a name is the asking script's own
// sound, then a shared MP3, then a melody.
PlayResult AudioRouter::startFile(const Spec& spec, Group group, bool effect,
                                  const std::string& script, DispatchDetail& detail) {
  const std::string& value = spec.text;
  if (isUrl(value)) return startPcm(spec, "", group, effect, script, detail);
  const std::size_t slash = value.find('/');
  if (slash != std::string::npos) {
    const std::string path = scriptMp3PathFor(value.substr(0, slash), value.substr(slash + 1));
    if (path.empty() || !assets_ || !assets_->hasFile(path)) {
      detail.message = quoted("no file", value);
      return PlayResult::NotFound;
    }
    return startPcm(spec, path, group, effect, script, detail);
  }
  const std::string path = findMp3(value, script);
  if (!path.empty()) return startPcm(spec, path, group, effect, script, detail);
  if (tone_ && assets_ && assets_->hasFile(melodyPathFor(value))) {
    if (!mayStartOneShot(group)) return PlayResult::Ok;
    if (!tone_->playMelodyFile(value, group)) return unavailable(detail, kSpeakerBusy);
    oneShotStarted(Sink::Tone, group, script, spec, effect);
    return PlayResult::Ok;
  }
  detail.message = quoted("nothing called", value);
  return PlayResult::NotFound;
}

PlayResult AudioRouter::startPcm(const Spec& spec, const std::string& path, Group group,
                                bool effect, const std::string& script, DispatchDetail& detail) {
  const PcmRequest request{spec, path, script, group, effect ? PlayAs::Effect : PlayAs::Once,
                           mayStartOneShot(group)};
  switch (pcm_->playSpec(request, detail)) {
    case PcmPlay::OneShot:
      oneShotStarted(Sink::Pcm, group, script, spec, effect);
      return PlayResult::Ok;
    case PcmPlay::Layer:
      setStatus(Group::App, displayName(spec));
      return PlayResult::Ok;
    case PcmPlay::Ignored:
      return PlayResult::Ok;
    case PcmPlay::Invalid:
      return PlayResult::Invalid;
    case PcmPlay::Unavailable:
      return unavailable(detail, kSpeakerBusy);
  }
  return PlayResult::NoSink;
}

// The one place a stored MP3's name becomes a path, so the probe and the sinks only ever see paths
// the name rule built. "" when no file answers to the name.
std::string AudioRouter::findMp3(const std::string& name, const std::string& script) const {
  if (!pcm_ || !assets_) return "";
  if (!script.empty()) {
    const std::string own = scriptMp3PathFor(script, name);
    if (!own.empty() && assets_->hasFile(own)) return own;
  }
  const std::string shared = mp3PathFor(name);
  return !shared.empty() && assets_->hasFile(shared) ? shared : "";
}

void AudioRouter::adoptPcm(const Spec& spec) {
  oneShotStarted(Sink::Pcm, Group::Alert, "", spec, false);
}

// A station ends the app's music and effects; a looping app one-shot would take it again, so that
// ends too. Alerts keep their place.
DispatchResult AudioRouter::playStream(const std::string& url, const std::string& label,
                                       DispatchDetail& detail) {
  if (!pcm_) {
    detail.message = "no audio output";
    return DispatchResult::Unavailable;
  }
  const DispatchResult result = pcm_->playStream(url, label, detail);
  if (result == DispatchResult::Ok) {
    if (oneShotGroup_ == Group::App) setRepeating(false);
    refreshStatus();
  }
  return result;
}

void AudioRouter::stop(Stop what, const std::string& script) {
  if (pcm_) pcm_->stopLayers(what, script);
  switch (what) {
    case Stop::All:
      stopOneShot();
      if (pcm_) pcm_->stopStream();
      break;
    case Stop::Alert:
      if (oneShotGroup_ == Group::Alert) stopOneShot();
      break;
    case Stop::App:
      if (oneShotGroup_ == Group::App) stopOneShot();
      break;
    case Stop::Radio:
      if (pcm_) pcm_->stopStream();
      break;
    case Stop::ScriptSounds:
      if (script.empty()) break;
      if (oneShotGroup_ == Group::App && oneShotOwner_ == script) stopOneShot();
      break;
    case Stop::ScriptMusic:
      if (script.empty()) break;
      if (repeating_ && oneShotGroup_ == Group::App && oneShotOwner_ == script) stopOneShot();
      break;
  }
  refreshStatus();
}

// Only the PCM output reads files while it plays: melodies are read whole before they start.
void AudioRouter::release(const std::string& path) {
  if (pcm_ && !path.empty()) pcm_->release(path);
}

void AudioRouter::tick(int64_t nowMs) {
  if (tone_) tone_->tick();
  if (track_) track_->tick();
  if (pcm_) pcm_->tick(nowMs);

  PcmError error;
  if (pcm_ && pcm_->takeError(error)) {
    GroupStatus& status = error.group == Group::App ? app_ : alert_;
    if (status.error != error.message) {
      status.error = std::move(error.message);
      statusChanged_ = true;
    }
    // An address that does not answer is not asked again and again.
    if (error.stopRepeat && repeating_ && repeat_.kind == Kind::File && isUrl(repeat_.text))
      setRepeating(false);
  }

  // A sound that ends at once must not spin: at most one new start per kRepeatGapMs. A speaker
  // that is taken is asked again; anything else ends the repeat.
  if (repeating_ && !oneShotActive() && nowMs - lastRepeatMs_ >= kRepeatGapMs) {
    lastRepeatMs_ = nowMs;
    const Spec spec = repeat_;
    const std::string owner = oneShotOwner_;
    const uint32_t seq = oneShotSeq_;
    DispatchDetail ignored;
    const PlayResult result = start(spec, oneShotGroup_, false, owner, ignored);
    if (result == PlayResult::Ok)
      oneShotSeq_ = seq;
    else if (result != PlayResult::NoSink)
      setRepeating(false);
  }
  refreshStatus();
}

uint32_t AudioRouter::repeatingAlert() const {
  return repeating_ && oneShotGroup_ == Group::Alert ? oneShotSeq_ : 0;
}

void AudioRouter::stopRepeatingAlert(uint32_t token) {
  if (!token || token != repeatingAlert()) return;
  stopOneShot();
  refreshStatus();
}

bool AudioRouter::oneShotActive() const {
  return (tone_ && tone_->isPlaying()) || (track_ && track_->isPlaying()) ||
         (pcm_ && pcm_->oneShotPlaying());
}

bool AudioRouter::alertPlaying() const {
  return oneShotGroup_ == Group::Alert && oneShotOn();
}

bool AudioRouter::appSoundPlaying() const {
  return (oneShotGroup_ == Group::App && oneShotOn()) || (pcm_ && pcm_->state().effects);
}

// Runs after the new sound starts and spares its sink.
// A one-shot with loop repeats; an effect never does.
void AudioRouter::oneShotStarted(Sink sink, Group group, const std::string& script,
                                 const Spec& spec, bool effect) {
  stopOthersThan(sink);
  ++oneShotSeq_;
  oneShotGroup_ = group;
  oneShotOwner_ = group == Group::App ? script : std::string();
  const bool repeats = spec.loop && !effect;
  if (repeats) repeat_ = spec;
  repeatOnSpeaker_ = sink == Sink::Pcm || (sink == Sink::Tone && tone_->sharesPcmOutput());
  setRepeating(repeats);
  setStatus(group, displayName(spec));
}

// The station stays away while a one-shot repeats on its speaker.
// A separate tone output or DFPlayer leaves it playing.
void AudioRouter::setRepeating(bool on) {
  repeating_ = on;
  const bool hold = on && repeatOnSpeaker_;
  if (hold == streamHeld_) return;
  streamHeld_ = hold;
  if (pcm_) pcm_->holdStream(hold);
}

void AudioRouter::stopOneShot() {
  if (tone_) tone_->stop();
  if (track_) track_->stop();
  if (pcm_) pcm_->stopOneShot();
  setRepeating(false);
  oneShotOwner_.clear();
}

void AudioRouter::stopOthersThan(Sink keep) {
  if (tone_ && keep != Sink::Tone) tone_->stop();
  if (track_ && keep != Sink::Track) track_->stop();
  if (pcm_ && keep != Sink::Pcm) pcm_->stopOneShot();
}

void AudioRouter::setStatus(Group group, const std::string& name) {
  GroupStatus& status = group == Group::Alert ? alert_ : app_;
  GroupStatus next;
  next.playing = true;
  next.name = name;
  if (next != status) {
    status = next;
    statusChanged_ = true;
  }
}

// The sinks say what plays; a layer they dropped by themselves loses its owner here. A one-shot
// the router did not start (the TC002's boot sound, the voice's answer) is the device's own alert.
void AudioRouter::refreshStatus() {
  const PcmState state = pcm_ ? pcm_->state() : PcmState{};
  if (state.groupKnown && state.group != oneShotGroup_) {
    oneShotGroup_ = state.group;
    oneShotOwner_.clear();
    setRepeating(false);
  }
  const bool oneShot = oneShotOn();
  if (!oneShot) {
    oneShotGroup_ = Group::Alert;
    oneShotOwner_.clear();
  }
  const bool effects = state.effects;
  const bool music = state.music;
  const bool alertNow = oneShot && oneShotGroup_ == Group::Alert;
  const bool appNow = (oneShot && oneShotGroup_ == Group::App) || effects || music;
  if (alert_.playing != alertNow) {
    alert_.playing = alertNow;
    statusChanged_ = true;
  }
  if (app_.playing != appNow) {
    app_.playing = appNow;
    statusChanged_ = true;
  }
}

bool AudioRouter::takeStatusChanged() {
  const bool changed = statusChanged_;
  statusChanged_ = false;
  return changed;
}

Caps AudioRouter::caps() const {
  Caps c = pcm_ ? pcm_->caps() : Caps{};
  c.rtttl = tone_ != nullptr;
  c.track = track_ != nullptr;
  return c;
}

bool AudioRouter::canPlay(const Choices& choices) const {
  for (const Spec& s : choices)
    if (!missingOutput(s)) return true;
  return false;
}

bool AudioRouter::check(const Choices& choices, Origin origin, DispatchDetail& detail) {
  if (!pcm_) return true;
  for (uint8_t i = 0; i < choices.count; ++i) {
    if (!pcm_->checkSpec(choices.items[i], detail)) {
      detail.field = specField(origin, choices.count > 1 ? i : -1, detail.field.c_str());
      return false;
    }
  }
  return true;
}

}
}
