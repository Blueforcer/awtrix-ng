#pragma once

#include <cstdint>
#include <string>

#include "core/Command.h"
#include "core/audio/AudioStats.h"
#include "core/audio/AnalysisSource.h"
#include "core/sound/PcmPlayback.h"

namespace awtrix {
namespace sound {

// One interface per physical output. Every sink takes the heard level per group and applies the
// group the router names when a sound starts; a change applies to what is playing.
class IToneSink {
 public:
  virtual ~IToneSink() = default;
  virtual void begin() = 0;
  virtual void setVolumes(const Volumes& volumes) = 0;
  // The router hands over RTTTL the parser accepted and names of melodies it found stored, so
  // false means the speaker cannot play now.
  virtual bool playRtttl(const std::string& rtttl, Group group) = 0;
  virtual bool playMelodyFile(const std::string& name, Group group) = 0;
  virtual void stop() = 0;
  virtual void tick() = 0;
  virtual bool isPlaying() const = 0;
  // True where melodies come out of the PCM sink's speaker, so they take it from the station.
  virtual bool sharesPcmOutput() const { return false; }
};

// The DFPlayer's folder addressing runs out at 2999.
constexpr int kMinTrack = 1;
constexpr int kMaxTrack = 2999;

// The card cannot be listed over the UART, so "the command went out" is all this can report.
class ITrackSink {
 public:
  virtual ~ITrackSink() = default;
  virtual void begin() = 0;
  virtual void setVolumes(const Volumes& volumes) = 0;
  virtual bool playTrack(int track, Group group) = 0;
  virtual void stop() = 0;
  virtual void tick() = 0;
  virtual bool isPlaying() const = 0;
};

// The one-shot is a single sound that plays once: an MP3, a clip, speech, a song played once, an
// MP3 from an address. oneShotPlaying() is true from the call that starts it until it has ended or
// failed, including while it is still being fetched, so the router never starts it twice.
// A sink that mixes also plays app layers: effects over each other and one looping music track.
// The stream plays at the radio level and gives way to any one-shot or layer.
class IPcmSink : public audio::IAnalysisSource {
 public:
  virtual ~IPcmSink() = default;
  virtual void setVolumes(const Volumes& volumes) = 0;

  virtual PcmPlay playSpec(const PcmRequest& request, DispatchDetail& detail) = 0;
  virtual Caps caps() const = 0;
  virtual bool checkSpec(const Spec& spec, DispatchDetail& detail) = 0;
  virtual void stopOneShot() = 0;
  virtual bool oneShotPlaying() const = 0;
  virtual void stopLayers(Stop what, const std::string& owner) = 0;
  virtual PcmState state() = 0;
  virtual bool takeError(PcmError& error) = 0;

  // Stops every layer playing from path (a file, or any file in that folder).
  // Returns once none has it open; other sounds keep playing.
  virtual void release(const std::string& path) = 0;

  virtual DispatchResult playStream(const std::string& url, const std::string& label,
                                    DispatchDetail& detail) = 0;
  virtual void stopStream() = 0;
  // While held, a station that gave way to a one-shot does not come back: the one-shot repeats
  // and would cut it off again at once.
  virtual void holdStream(bool held) = 0;

  virtual void tick(int64_t nowMs) = 0;

  // The analysed frame that is audible at nowMs; false when nothing plays or the output cannot
  // analyse. Asking is what switches the analysis on.
  bool analysis(int64_t nowMs, audio::FrameStats& out) override = 0;
  virtual uint32_t underruns() const = 0;
  virtual uint32_t decodeUs() const = 0;
  virtual uint32_t starvedMs() const = 0;
  virtual uint32_t bufferBytes() const = 0;
};

// Injected, so the router never opens a filesystem. It asks about paths it built itself.
class IAssetProbe {
 public:
  virtual ~IAssetProbe() = default;
  virtual bool hasFile(const std::string& path) const = 0;
};

}
}
