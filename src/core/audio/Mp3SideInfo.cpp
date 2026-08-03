#include "core/audio/Mp3SideInfo.h"

namespace awtrix {
namespace mp3 {

// MPEG-1 Layer III side info size. MPEG-2/2.5 use 9 and 17, but the decoder rejects those first.
int sideInfoBytes(const FrameHeader& header) { return header.channels() == 1 ? 17 : 32; }

bool parseSideInfo(BitReader& bits, const FrameHeader& header, SideInfo& out) {
  const int channels = header.channels();

  out.mainDataBegin = static_cast<int>(bits.read(9));
  // private_bits, whose width depends on the channel count.
  bits.skip(channels == 1 ? 5 : 3);

  for (int ch = 0; ch < channels; ++ch)
    for (int band = 0; band < 4; ++band) out.scfsi[ch][band] = bits.read(1) != 0;

  for (int gr = 0; gr < kGranules; ++gr) {
    for (int ch = 0; ch < channels; ++ch) {
      GranuleInfo& g = out.granules[gr][ch];
      g.part2_3Length = static_cast<int>(bits.read(12));
      g.bigValues = static_cast<int>(bits.read(9));
      g.globalGain = static_cast<int>(bits.read(8));
      g.scalefacCompress = static_cast<int>(bits.read(4));
      g.windowSwitching = bits.read(1) != 0;

      if (g.windowSwitching) {
        g.blockType = static_cast<int>(bits.read(2));
        g.mixedBlock = bits.read(1) != 0;
        for (int i = 0; i < 2; ++i) g.tableSelect[i] = static_cast<int>(bits.read(5));
        g.tableSelect[2] = 0;
        for (int i = 0; i < 3; ++i) g.subblockGain[i] = static_cast<int>(bits.read(3));

        // With window switching there are only two Huffman regions, so the region counts are not
        // transmitted; these are the fixed values the standard prescribes.
        g.region0Count = (g.blockType == 2 && !g.mixedBlock) ? 8 : 7;
        g.region1Count = 20 - g.region0Count;
      } else {
        g.blockType = 0;
        g.mixedBlock = false;
        for (int i = 0; i < 3; ++i) g.tableSelect[i] = static_cast<int>(bits.read(5));
        g.region0Count = static_cast<int>(bits.read(4));
        g.region1Count = static_cast<int>(bits.read(3));
        for (int i = 0; i < 3; ++i) g.subblockGain[i] = 0;
      }

      g.preflag = bits.read(1) != 0;
      g.scalefacScale = bits.read(1) != 0;
      g.count1TableSelect = bits.read(1) != 0;
    }
  }

  return !bits.overrun();
}

}
}
