#pragma once

#include <cstdint>
#include <string>

#include "core/Command.h"

namespace awtrix {
namespace sound {

// One interface per physical output.
class IToneSink {
 public:
  virtual ~IToneSink() = default;
  virtual void begin() = 0;
  virtual void setVolume(uint8_t percent) = 0;
  virtual bool playRtttl(const std::string& rtttl) = 0;
  // False means no melody is stored under that name.
  virtual bool playMelodyFile(const std::string& name) = 0;
  virtual void stop() = 0;
  virtual void tick() = 0;
  virtual bool isPlaying() const = 0;
};

// The DFPlayer's folder addressing runs out at 2999.
constexpr int kMinTrack = 1;
constexpr int kMaxTrack = 2999;

// The card cannot be listed over the UART, so "the command went out" is all this can report.
class ITrackSink {
 public:
  virtual ~ITrackSink() = default;
  virtual void begin() = 0;
  virtual void setVolume(uint8_t percent) = 0;
  virtual bool playTrack(int track) = 0;
  virtual void stop() = 0;
  virtual void tick() = 0;
  virtual bool isPlaying() const = 0;
};

// Stored MP3s and the stream share the output but not the gain.
class IPcmSink {
 public:
  virtual ~IPcmSink() = default;
  virtual void setSoundVolume(uint8_t percent) = 0;
  virtual void setStreamVolume(uint8_t percent) = 0;

  // The caller hands in a validated "/MP3/<name>.mp3".
  virtual bool playMp3(const std::string& path) = 0;
  virtual void stopMp3() = 0;
  virtual bool mp3Playing() const = 0;

  virtual DispatchResult playStream(const std::string& url, const std::string& label,
                                    DispatchDetail& detail) = 0;
  virtual void stopStream() = 0;

  virtual void tick(int64_t nowMs) = 0;

  virtual uint32_t underruns() const { return 0; }
  virtual uint32_t decodeUs() const { return 0; }
  virtual uint32_t starvedMs() const { return 0; }
  virtual uint32_t bufferBytes() const { return 0; }
};

// Injected, so the router never opens a filesystem.
class IAssetProbe {
 public:
  virtual ~IAssetProbe() = default;
  virtual bool hasMp3(const std::string& name) const = 0;
};

}
}
