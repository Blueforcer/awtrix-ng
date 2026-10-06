#pragma once
#include <string>

namespace awtrix::tc002::mcu {
// Pure transformation; no device access. Throws on any unsupported input.
// The caller also checks the final package against the release's expected SHA-256.
std::string assembleFirmware(const std::string& base, const std::string& extension);
}
