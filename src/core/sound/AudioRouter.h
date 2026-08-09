#pragma once

#include <cstdint>
#include <string>

#include "core/Command.h"
#include "core/sound/AudioSinks.h"

namespace awtrix {
namespace sound {

// Auto is the only value that consults more than one sink; the rest never fall back.
enum class Source : uint8_t { Auto, Mp3, Melody, Track, Rtttl };

enum class StopScope : uint8_t { Sounds, Stream, All };

enum class PlayResult : uint8_t { Ok, Muted, NotFound, NoSink, Invalid };

struct Caps {
  bool buzzer = false;
  bool track = false;
  bool mp3 = false;
  bool radio = false;
};

class AudioRouter final {
 public:
  void setTone(IToneSink* tone) { tone_ = tone; }
  void setTrack(ITrackSink* track) { track_ = track; }
  void setPcm(IPcmSink* pcm) { pcm_ = pcm; }
  void setAssets(const IAssetProbe* assets) { assets_ = assets; }

  PlayResult play(Source source, const std::string& value, DispatchDetail& detail);
  DispatchResult playStream(const std::string& url, const std::string& label,
                            DispatchDetail& detail);
  void stop(StopScope scope);

  // One-shots only; a stream keeps playing.
  void setMuted(bool muted) { muted_ = muted; }

  // No-ops are dropped: a DFPlayer takes ten bytes at 9600 baud per change.
  void setVolumes(uint8_t buzzerPercent, uint8_t trackPercent, uint8_t mp3Percent,
                  uint8_t streamPercent);

  void tick(int64_t nowMs);

  // One-shots only, or a looping notification would wait for a station to end.
  bool isPlaying() const;
  Caps caps() const;

 private:
  enum class Sink : uint8_t { None, Tone, Track, Pcm };

  PlayResult playAuto(const std::string& name, DispatchDetail& detail);
  void stopOthersThan(Sink keep);

  IToneSink* tone_ = nullptr;
  ITrackSink* track_ = nullptr;
  IPcmSink* pcm_ = nullptr;
  const IAssetProbe* assets_ = nullptr;
  bool muted_ = false;
  // -1 is "never pushed"; a missing sink is skipped so one attached later still gets its value.
  int buzzerVolume_ = -1;
  int trackVolume_ = -1;
  int mp3Volume_ = -1;
  int streamVolume_ = -1;
};

}
}
