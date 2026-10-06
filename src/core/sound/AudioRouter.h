#pragma once

#include <cstdint>
#include <string>

#include "core/Command.h"
#include "core/sound/AudioSinks.h"
#include "core/sound/Sound.h"
#include "core/sound/SoundSpec.h"

namespace awtrix {
namespace sound {

enum class PlayResult : uint8_t { Ok, NotFound, NoSink, Invalid };

struct GroupStatus {
  bool playing = false;
  std::string name;
  std::string error;
  bool operator==(const GroupStatus& o) const {
    return playing == o.playing && name == o.name && error == o.error;
  }
  bool operator!=(const GroupStatus& o) const { return !(*this == o); }
};

class AudioRouter final {
 public:
  void setTone(IToneSink* tone);
  void setTrack(ITrackSink* track);
  void setPcm(IPcmSink* pcm);
  void setAssets(const IAssetProbe* assets) { assets_ = assets; }

  // The four settings, in percent. A sink attached later gets the current levels at once.
  void setVolumes(int master, int radio, int app, int alert);
  const Volumes& volumes() const { return volumes_; }

  // group is Alert or App; script is the script whose own call this is, "" for anyone else.
  PlayResult play(const Choices& choices, Group group, const std::string& script,
                  DispatchDetail& detail);
  PlayResult playEffect(const Choices& choices, const std::string& script, DispatchDetail& detail);
  void adoptPcm(const Spec& spec);
  DispatchResult playStream(const std::string& url, const std::string& label,
                            DispatchDetail& detail);
  void stop(Stop what, const std::string& script = std::string());
  // Called before sound files are deleted or replaced. See IPcmSink::release.
  void release(const std::string& path);
  void tick(int64_t nowMs);

  // The alert that repeats now, as a token for stopRepeatingAlert(); 0 when none does. Asked right
  // after play() started an alert, it names that alert.
  uint32_t repeatingAlert() const;
  // Ends that alert's repeating, and nothing that replaced it.
  void stopRepeatingAlert(uint32_t token);

  // A one-shot that repeats counts between its plays as well.
  bool alertPlaying() const;
  // An app one-shot or an effect; never the music. What sound.playing() answers.
  bool appSoundPlaying() const;
  Caps caps() const;
  // Whether some entry is a kind this clock has the output for; files are not looked up.
  bool canPlay(const Choices& choices) const;
  // Song and speech text judged by the sink that would play them; true where none exists.
  bool check(const Choices& choices, Origin origin, DispatchDetail& detail);

  const GroupStatus& alertStatus() const { return alert_; }
  const GroupStatus& appStatus() const { return app_; }
  // True once after either status changed.
  bool takeStatusChanged();

 private:
  enum class Sink : uint8_t { None, Tone, Track, Pcm };
  static constexpr int64_t kRepeatGapMs = 250;

  PlayResult startFirst(const Choices& choices, Group group, bool effect,
                        const std::string& script, DispatchDetail& detail);
  PlayResult start(const Spec& spec, Group group, bool effect, const std::string& script,
                   DispatchDetail& detail);
  PlayResult startFile(const Spec& spec, Group group, bool effect, const std::string& script,
                       DispatchDetail& detail);
  PlayResult startPcm(const Spec& spec, const std::string& path, Group group, bool effect,
                      const std::string& script, DispatchDetail& detail);
  std::string findMp3(const std::string& name, const std::string& script) const;
  const char* missingOutput(const Spec& spec) const;
  bool mayStartOneShot(Group group) const { return group != Group::App || !alertPlaying(); }
  bool oneShotActive() const;
  bool oneShotOn() const { return repeating_ || oneShotActive(); }
  void oneShotStarted(Sink sink, Group group, const std::string& script, const Spec& spec,
                      bool effect);
  void setRepeating(bool on);
  void stopOneShot();
  void stopOthersThan(Sink keep);
  void setStatus(Group group, const std::string& name);
  void refreshStatus();

  IToneSink* tone_ = nullptr;
  ITrackSink* track_ = nullptr;
  IPcmSink* pcm_ = nullptr;
  const IAssetProbe* assets_ = nullptr;

  Volumes volumes_;
  bool volumesPushed_ = false;

  // Counts the one-shots started; a repeat keeps the number of the play it repeats.
  uint32_t oneShotSeq_ = 0;
  Group oneShotGroup_ = Group::Alert;
  std::string oneShotOwner_;
  // A one-shot with loop: started again once it ended, until it is stopped or replaced.
  bool repeating_ = false;
  // Whether the repeating one-shot plays on the station's speaker; only then is the station held.
  bool repeatOnSpeaker_ = false;
  bool streamHeld_ = false;
  Spec repeat_;
  int64_t lastRepeatMs_ = 0;

  GroupStatus alert_;
  GroupStatus app_;
  bool statusChanged_ = false;
};

}
}
