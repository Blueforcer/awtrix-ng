#include "platform/posix/Digest.h"

#include "platform/posix/sha256.h"

namespace awtrix::posix {

std::string sha256Hex(std::string_view text) {
  sha256_state state;
  sha256_init(&state);
  sha256_update(&state, text.data(), text.size());
  uint8_t digest[32];
  sha256_final(&state, digest);
  char hex[65];
  sha256_hex(digest, hex);
  return std::string(hex, 64);
}

}
