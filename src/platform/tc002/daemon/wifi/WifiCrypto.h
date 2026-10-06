#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace awtrix {
namespace tc002d {
namespace wifi {

using Sha1Digest = std::array<uint8_t, 20>;
using WpaPsk = std::array<uint8_t, 32>;

class Sha1 {
 public:
  Sha1();
  void update(const void* data, std::size_t size);
  Sha1Digest finish();

 private:
  void compress(const uint8_t* block);
  uint32_t state_[5];
  uint64_t length_ = 0;
  uint8_t block_[64];
  std::size_t used_ = 0;
};

Sha1Digest sha1(const void* data, std::size_t size);
Sha1Digest hmacSha1(const void* key, std::size_t keySize, const void* data, std::size_t size);
void pbkdf2HmacSha1(std::string_view password, const void* salt, std::size_t saltSize,
                    uint32_t iterations, uint8_t* out, std::size_t outSize);
// IEEE 802.11i PSK: PBKDF2-HMAC-SHA1(passphrase, ssid, 4096, 32).
WpaPsk deriveWpaPsk(std::string_view passphrase, std::string_view ssid);
void secureErase(void* data, std::size_t size);

}
}
}
