#pragma once

#include <cstdint>

#include "core/audio/Mp3Bitstream.h"
#include "core/audio/Mp3Frame.h"

namespace awtrix {
namespace mp3 {

inline constexpr int kGranules = 2;
inline constexpr int kMaxChannels = 2;
inline constexpr int kSamplesPerGranule = 576;

struct GranuleInfo {
  int part2_3Length = 0;
  int bigValues = 0;
  int globalGain = 0;
  int scalefacCompress = 0;
  bool windowSwitching = false;
  int blockType = 0;
  bool mixedBlock = false;
  int tableSelect[3] = {0, 0, 0};
  int subblockGain[3] = {0, 0, 0};
  int region0Count = 0;
  int region1Count = 0;
  bool preflag = false;
  bool scalefacScale = false;
  bool count1TableSelect = false;
};

struct SideInfo {
  int mainDataBegin = 0;
  bool scfsi[kMaxChannels][4] = {};
  GranuleInfo granules[kGranules][kMaxChannels];
};

int sideInfoBytes(const FrameHeader& header);

bool parseSideInfo(BitReader& bits, const FrameHeader& header, SideInfo& out);

}
}
