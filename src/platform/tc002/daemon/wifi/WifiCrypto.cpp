#include "platform/tc002/daemon/wifi/WifiCrypto.h"

#include <cstring>

namespace awtrix {
namespace tc002d {
namespace wifi {
namespace {

uint32_t rotate(uint32_t value, unsigned bits) { return (value << bits) | (value >> (32 - bits)); }

class HmacSha1 {
 public:
  HmacSha1(const void* key, std::size_t keySize) {
    uint8_t pad[64] = {};
    if (keySize > sizeof pad) {
      const Sha1Digest digest = sha1(key, keySize);
      std::memcpy(pad, digest.data(), digest.size());
    } else if (keySize) {
      std::memcpy(pad, key, keySize);
    }
    for (uint8_t& byte : pad) byte ^= 0x36;
    inner_.update(pad, sizeof pad);
    for (uint8_t& byte : pad) byte ^= 0x36 ^ 0x5c;
    outer_.update(pad, sizeof pad);
    secureErase(pad, sizeof pad);
  }
  ~HmacSha1() {
    secureErase(&inner_, sizeof inner_);
    secureErase(&outer_, sizeof outer_);
  }

  Sha1Digest compute(const void* first, std::size_t firstSize, const void* second = nullptr,
                     std::size_t secondSize = 0) const {
    Sha1 inner = inner_;
    inner.update(first, firstSize);
    if (secondSize) inner.update(second, secondSize);
    Sha1Digest digest = inner.finish();
    Sha1 outer = outer_;
    outer.update(digest.data(), digest.size());
    return outer.finish();
  }

 private:
  Sha1 inner_;
  Sha1 outer_;
};

}

Sha1::Sha1() : state_{0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u, 0xc3d2e1f0u}, block_{} {}

void Sha1::compress(const uint8_t* block) {
  uint32_t w[80];
  for (unsigned i = 0; i < 16; ++i)
    w[i] = uint32_t(block[4 * i]) << 24 | uint32_t(block[4 * i + 1]) << 16 |
           uint32_t(block[4 * i + 2]) << 8 | uint32_t(block[4 * i + 3]);
  for (unsigned i = 16; i < 80; ++i) w[i] = rotate(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
  uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3], e = state_[4];
  for (unsigned i = 0; i < 80; ++i) {
    uint32_t f, k;
    if (i < 20) { f = (b & c) | (~b & d); k = 0x5a827999u; }
    else if (i < 40) { f = b ^ c ^ d; k = 0x6ed9eba1u; }
    else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8f1bbcdcu; }
    else { f = b ^ c ^ d; k = 0xca62c1d6u; }
    const uint32_t next = rotate(a, 5) + f + e + k + w[i];
    e = d; d = c; c = rotate(b, 30); b = a; a = next;
  }
  state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d; state_[4] += e;
}

void Sha1::update(const void* data, std::size_t size) {
  const uint8_t* bytes = static_cast<const uint8_t*>(data);
  length_ += uint64_t(size) * 8;
  while (size) {
    const std::size_t take = size < sizeof block_ - used_ ? size : sizeof block_ - used_;
    std::memcpy(block_ + used_, bytes, take);
    used_ += take; bytes += take; size -= take;
    if (used_ == sizeof block_) { compress(block_); used_ = 0; }
  }
}

Sha1Digest Sha1::finish() {
  block_[used_++] = 0x80;
  if (used_ > 56) {
    std::memset(block_ + used_, 0, sizeof block_ - used_);
    compress(block_);
    used_ = 0;
  }
  std::memset(block_ + used_, 0, 56 - used_);
  for (unsigned i = 0; i < 8; ++i) block_[56 + i] = uint8_t(length_ >> (56 - 8 * i));
  compress(block_);
  used_ = 0;
  Sha1Digest digest;
  for (unsigned i = 0; i < 5; ++i)
    for (unsigned j = 0; j < 4; ++j) digest[4 * i + j] = uint8_t(state_[i] >> (24 - 8 * j));
  return digest;
}

Sha1Digest sha1(const void* data, std::size_t size) {
  Sha1 hash;
  hash.update(data, size);
  return hash.finish();
}

Sha1Digest hmacSha1(const void* key, std::size_t keySize, const void* data, std::size_t size) {
  return HmacSha1(key, keySize).compute(data, size);
}

void pbkdf2HmacSha1(std::string_view password, const void* salt, std::size_t saltSize,
                    uint32_t iterations, uint8_t* out, std::size_t outSize) {
  const HmacSha1 prf(password.data(), password.size());
  for (uint32_t blockIndex = 1; outSize; ++blockIndex) {
    const uint8_t counter[4] = {uint8_t(blockIndex >> 24), uint8_t(blockIndex >> 16),
                                uint8_t(blockIndex >> 8), uint8_t(blockIndex)};
    Sha1Digest u = prf.compute(salt, saltSize, counter, sizeof counter);
    Sha1Digest t = u;
    for (uint32_t i = 1; i < iterations; ++i) {
      u = prf.compute(u.data(), u.size());
      for (std::size_t j = 0; j < t.size(); ++j) t[j] ^= u[j];
    }
    const std::size_t take = outSize < t.size() ? outSize : t.size();
    std::memcpy(out, t.data(), take);
    out += take; outSize -= take;
    secureErase(u.data(), u.size());
    secureErase(t.data(), t.size());
  }
}

WpaPsk deriveWpaPsk(std::string_view passphrase, std::string_view ssid) {
  WpaPsk psk;
  pbkdf2HmacSha1(passphrase, ssid.data(), ssid.size(), 4096, psk.data(), psk.size());
  return psk;
}

void secureErase(void* data, std::size_t size) {
  volatile uint8_t* bytes = static_cast<volatile uint8_t*>(data);
  while (size--) *bytes++ = 0;
}

}
}
}
