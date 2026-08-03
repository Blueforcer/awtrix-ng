#pragma once

#include <cstddef>
#include <cstdint>

namespace awtrix {
namespace mp3 {

enum class Version : uint8_t { Mpeg2_5, Reserved, Mpeg2, Mpeg1 };
enum class Layer : uint8_t { Reserved, LayerIII, LayerII, LayerI };
enum class ChannelMode : uint8_t { Stereo, JointStereo, DualChannel, Mono };

struct FrameHeader {
  Version version = Version::Reserved;
  Layer layer = Layer::Reserved;
  bool hasCrc = false;
  int bitrateKbps = 0;
  int sampleRateHz = 0;
  bool padding = false;
  ChannelMode channelMode = ChannelMode::Stereo;
  int modeExtension = 0;

  int channels() const { return channelMode == ChannelMode::Mono ? 1 : 2; }

  int samplesPerFrame() const { return version == Version::Mpeg1 ? 1152 : 576; }

  int frameBytes() const;
};

bool parseHeader(const uint8_t* data, std::size_t bytes, FrameHeader& out);

bool isSupported(const FrameHeader& h);

bool findSync(const uint8_t* data, std::size_t bytes, std::size_t from, std::size_t& at);

}
}
