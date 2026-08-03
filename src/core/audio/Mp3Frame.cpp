#include "core/audio/Mp3Frame.h"

namespace awtrix {
namespace mp3 {

namespace {

constexpr int kBitrateMpeg1[16] = {0,   32,  40,  48,  56,  64,  80,  96,
                                   112, 128, 160, 192, 224, 256, 320, 0};
constexpr int kBitrateMpeg2[16] = {0,  8,  16, 24, 32,  40,  48,  56,
                                   64, 80, 96, 112, 128, 144, 160, 0};

// Rows are indexed by the Version enum, so row 1 (Reserved) is deliberately zero.
constexpr int kSampleRate[4][3] = {
    {11025, 12000, 8000},
    {0, 0, 0},
    {22050, 24000, 16000},
    {44100, 48000, 32000},
};

}

int FrameHeader::frameBytes() const {
  if (bitrateKbps <= 0 || sampleRateHz <= 0) return 0;
  // The coefficient is samplesPerFrame / 8: 1152/8 for MPEG-1, 576/8 for MPEG-2 and 2.5.
  const int coefficient = (version == Version::Mpeg1) ? 144 : 72;
  return coefficient * bitrateKbps * 1000 / sampleRateHz + (padding ? 1 : 0);
}

bool parseHeader(const uint8_t* data, std::size_t bytes, FrameHeader& out) {
  if (data == nullptr || bytes < 4) return false;

  if (data[0] != 0xFF || (data[1] & 0xE0) != 0xE0) return false;

  const auto version = static_cast<Version>((data[1] >> 3) & 0x03);
  if (version == Version::Reserved) return false;

  const auto layer = static_cast<Layer>((data[1] >> 1) & 0x03);
  if (layer != Layer::LayerIII) return false;

  const unsigned bitrateIndex = (data[2] >> 4) & 0x0F;
  if (bitrateIndex == 15) return false;

  const unsigned sampleRateIndex = (data[2] >> 2) & 0x03;
  if (sampleRateIndex == 3) return false;

  out.version = version;
  out.layer = layer;
  // Bit 0 of byte 1 is "protection absent", so a clear bit means a CRC follows the header.
  out.hasCrc = (data[1] & 0x01) == 0;
  out.bitrateKbps =
      (version == Version::Mpeg1) ? kBitrateMpeg1[bitrateIndex] : kBitrateMpeg2[bitrateIndex];
  out.sampleRateHz = kSampleRate[static_cast<unsigned>(version)][sampleRateIndex];
  out.padding = (data[2] & 0x02) != 0;
  out.channelMode = static_cast<ChannelMode>((data[3] >> 6) & 0x03);
  out.modeExtension = (data[3] >> 4) & 0x03;
  return true;
}

bool isSupported(const FrameHeader& h) {
  return h.version == Version::Mpeg1 && h.layer == Layer::LayerIII;
}

// Scans for a frame start. 0xFF alone is far too common in audio data, so every candidate has to
// parse as a full header before it counts as sync.
bool findSync(const uint8_t* data, std::size_t bytes, std::size_t from, std::size_t& at) {
  if (data == nullptr || bytes < 4) return false;
  FrameHeader scratch{};
  for (std::size_t i = from; i + 4 <= bytes; ++i) {
    if (data[i] != 0xFF) continue;
    if (parseHeader(data + i, bytes - i, scratch)) {
      at = i;
      return true;
    }
  }
  return false;
}

}
}
