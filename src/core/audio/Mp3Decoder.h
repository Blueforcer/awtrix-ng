#pragma once

#include <cstddef>
#include <cstdint>

#include "core/audio/Mp3Frame.h"
#include "core/audio/Mp3SideInfo.h"

namespace awtrix {
namespace mp3 {

enum class DecodeStatus : uint8_t {
  Ok,
  NeedMoreData,
  NoSync,
  Unsupported,
  CorruptFrame,
};

struct DecodeResult {
  DecodeStatus status = DecodeStatus::NeedMoreData;
  std::size_t bytesConsumed = 0;
  int sampleRateHz = 0;
  int channels = 0;
  int samples = 0;
};

inline constexpr int kMaxSamplesPerFrame = 1152;
inline constexpr int kMaxPcmPerFrame = kMaxSamplesPerFrame * 2;

class Decoder {
 public:
  Decoder() { reset(); }

  DecodeResult decode(const uint8_t* data, std::size_t bytes, int16_t* pcm);

  void reset();

 private:
  bool decodeGranule(const FrameHeader& header, const SideInfo& side, int granule,
                     class BitReader& bits, int16_t* pcm);
  void requantize(const FrameHeader& header, const GranuleInfo& granule,
                  const int* scalefactors, int channel);
  void applyStereo(const FrameHeader& header, const SideInfo& side, int granule);
  void reorderShortBlocks(const FrameHeader& header, const GranuleInfo& granule, int channel);
  void reduceAliasing(const GranuleInfo& granule, int channel);
  void inverseMdct(const GranuleInfo& granule, int channel);
  void synthesise(int channel, int granule, int16_t* pcm, int channels);

  float spectrum_[kMaxChannels][kSamplesPerGranule];
  float overlap_[kMaxChannels][32][18];
  // Polyphase synthesis history, used as a 1024-entry ring indexed by synthesisOffset_.
  float synthesis_[kMaxChannels][1024];
  int synthesisOffset_[kMaxChannels];
  int scalefactors_[kMaxChannels][39];
  int intensityPosition_[kMaxChannels][39];

  // 511 is the largest main_data_begin a 9-bit field can express, i.e. how far back a frame may
  // reach into earlier frames' bytes; the extra 1536 holds the current frame's own main data.
  static constexpr std::size_t kReservoirBytes = 511;
  uint8_t reservoir_[kReservoirBytes + 1536];
  std::size_t reservoirFill_ = 0;
};

}
}
