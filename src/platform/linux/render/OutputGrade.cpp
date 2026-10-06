#include "core/render/ColorGrade.h"

#include <algorithm>

namespace awtrix::render {
void ColorGrade::setOutput(const OutputTable* table) {
  if (table == output_) return;
  output_ = table;
  rebuild();
}

// Rounds once, to the code whose light is nearest. Codes that emit the same light share one answer,
// the lowest of them, so codes a panel leaves dark are never chosen over zero.
uint8_t OutputGrade::encodeOutput(uint32_t light16) const {
  if (!output_) return static_cast<uint8_t>((light16 + 128) / 257);
  const OutputTable& table = *output_;
  const auto above = std::lower_bound(table.begin(), table.end(), light16);
  if (above == table.begin()) return 0;
  if (above == table.end()) return 255;
  const uint16_t below = *(above - 1);
  if (light16 - below > static_cast<uint32_t>(*above) - light16)
    return static_cast<uint8_t>(above - table.begin());
  return static_cast<uint8_t>(std::lower_bound(table.begin(), above, below) - table.begin());
}

void OutputGrade::prepareOutput() {
  if (output_) {
    full_.resize(3 * 256);
    floorCode_ = static_cast<uint8_t>(std::upper_bound(output_->begin(), output_->end(), 0) - output_->begin());
  } else {
    full_.clear();
  }
}

// At a panel's lowest lit code a channel is either off or at the floor, so a pixel's channels are
// settled together there. Dimming keeps a pixel that full brightness shows: its strongest channel
// stays at the floor. Every channel that wants at least half the light of the strongest comes on
// with it.
void OutputGrade::settleFloor(uint8_t brightness, uint8_t r, uint8_t g, uint8_t b, uint8_t* codes) const {
  const uint32_t wanted[3] = {full_[r], full_[256 + g], full_[512 + b]};
  const uint32_t strongest = std::max({wanted[0], wanted[1], wanted[2]});
  if (!(codes[0] | codes[1] | codes[2]) &&
      (brightness == 0 || 2 * strongest <= (*output_)[floorCode_]))
    return;
  for (int ch = 0; ch < 3; ++ch)
    if (!codes[ch] && 2 * wanted[ch] >= strongest) codes[ch] = floorCode_;
}

}
