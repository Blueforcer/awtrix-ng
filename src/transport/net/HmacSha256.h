#pragma once

#include <cstddef>
#include <cstdint>

namespace awtrix {

// HMAC-SHA256 from the platform's crypto library: mbedTLS on the ESP32, OpenSSL on Linux. out takes
// 32 bytes.
bool hmacSha256(const uint8_t* key, std::size_t keyLength, const uint8_t* data, std::size_t length,
                uint8_t* out);

}
