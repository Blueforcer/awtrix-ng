#include "transport/net/HmacSha256.h"

#if defined(ESP_PLATFORM)
#include <mbedtls/md.h>
#else
#include <openssl/evp.h>
#include <openssl/hmac.h>
#endif

namespace awtrix {

bool hmacSha256(const uint8_t* key, std::size_t keyLength, const uint8_t* data, std::size_t length,
                uint8_t* out) {
#if defined(ESP_PLATFORM)
  const mbedtls_md_info_t* sha256 = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  return sha256 && mbedtls_md_hmac(sha256, key, keyLength, data, length, out) == 0;
#else
  unsigned int written = 32;
  return HMAC(EVP_sha256(), key, static_cast<int>(keyLength), data, length, out, &written) != nullptr &&
         written == 32;
#endif
}

}
