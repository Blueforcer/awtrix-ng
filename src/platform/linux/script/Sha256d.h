#pragma once

#include <cstdint>

namespace awtrix::linux_script {

// sha256d of one 80-byte block header for nonce after nonce: what does not depend on the nonce is
// hashed once per header, and a nonce costs only the part of both hashes its top word needs.
class Sha256d {
 public:
  explicit Sha256d(const uint8_t header[80]);
  // Bytes 31 to 28 of the hash with this nonce: the top 32 bits of the number meets() compares.
  uint32_t top(uint32_t nonce) const;
  // The whole hash with this nonce, bytes as SHA-256 writes them.
  void hash(uint32_t nonce, uint8_t out[32]) const;

 private:
  void first(uint32_t nonce, uint32_t out[8]) const;

  uint32_t mid_[8];
  uint32_t round3_[8];
  uint32_t w16_, w17_, w18_, w19_, w31_, w32_;
};

}
