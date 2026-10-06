#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace awtrix::tc002 {
// Canonical padded Base64 of little-endian signed samples, independent of host byte order.
std::string encodePcm16(const std::vector<int16_t>& samples);
bool decodePcm16(std::string_view encoded, std::vector<int16_t>& samples);
}
