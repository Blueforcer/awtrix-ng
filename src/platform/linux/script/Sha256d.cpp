#include "platform/linux/script/Sha256d.h"

#include <cstring>

#include "platform/posix/sha256.h"

namespace awtrix::linux_script {
namespace {

// The padding words of the two blocks after the header's first 64 bytes and after the first hash.
constexpr uint32_t kOne = 0x80000000u;
constexpr uint32_t kHeaderBits = 640;
constexpr uint32_t kHashBits = 256;

constexpr uint32_t rotr(uint32_t x, int n) { return x >> n | x << (32 - n); }
constexpr uint32_t Sigma0(uint32_t a) { return rotr(a ^ rotr(a ^ rotr(a, 9), 11), 2); }
constexpr uint32_t Sigma1(uint32_t e) { return rotr(e ^ rotr(e ^ rotr(e, 14), 5), 6); }
constexpr uint32_t sigma0(uint32_t w) { return rotr(w ^ rotr(w, 11), 7) ^ w >> 3; }
constexpr uint32_t sigma1(uint32_t w) { return rotr(w ^ rotr(w, 2), 17) ^ w >> 10; }
constexpr uint32_t choose(uint32_t e, uint32_t f, uint32_t g) { return g ^ (e & (f ^ g)); }
constexpr uint32_t majority(uint32_t a, uint32_t b, uint32_t c) { return b ^ ((a ^ b) & (b ^ c)); }

uint32_t bigEndian(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) << 24 | static_cast<uint32_t>(p[1]) << 16 | static_cast<uint32_t>(p[2]) << 8 | p[3];
}

}

// Round i on the state s, whose eight words take the roles a to h in turn; kw is k[i] + w[i].
#define ROUND(s, i, kw)                                                                            \
  do {                                                                                             \
    const uint32_t t = s[(71 - (i)) % 8] + Sigma1(s[(68 - (i)) % 8]) +                             \
                       choose(s[(68 - (i)) % 8], s[(69 - (i)) % 8], s[(70 - (i)) % 8]) + (kw);     \
    s[(67 - (i)) % 8] += t;                                                                        \
    s[(71 - (i)) % 8] = t + Sigma0(s[(64 - (i)) % 8]) +                                            \
                        majority(s[(64 - (i)) % 8], s[(65 - (i)) % 8], s[(66 - (i)) % 8]);         \
  } while (0)
// Round i when only its new e is read later.
#define ROUND_E(s, i, kw)                                                                          \
  s[(67 - (i)) % 8] += s[(71 - (i)) % 8] + Sigma1(s[(68 - (i)) % 8]) +                             \
                       choose(s[(68 - (i)) % 8], s[(69 - (i)) % 8], s[(70 - (i)) % 8]) + (kw)
#define EXPAND(w, i) w[i] = sigma1(w[(i) - 2]) + w[(i) - 7] + sigma0(w[(i) - 15]) + w[(i) - 16]
#define STEP(s, w, i) ROUND(s, i, sha256_round_constants[i] + w[i])
#define STEP4(s, w, i) STEP(s, w, i); STEP(s, w, (i) + 1); STEP(s, w, (i) + 2); STEP(s, w, (i) + 3)
#define NEXT(s, w, i) EXPAND(w, i); STEP(s, w, i)
#define NEXT4(s, w, i) NEXT(s, w, i); NEXT(s, w, (i) + 1); NEXT(s, w, (i) + 2); NEXT(s, w, (i) + 3)

Sha256d::Sha256d(const uint8_t header[80]) {
  std::memcpy(mid_, sha256_initial_words, sizeof mid_);
  sha256_compress(mid_, header);
  const uint32_t w0 = bigEndian(header + 64), w1 = bigEndian(header + 68), w2 = bigEndian(header + 72);
  uint32_t s[8];
  std::memcpy(s, mid_, sizeof s);
  ROUND(s, 0, sha256_round_constants[0] + w0);
  ROUND(s, 1, sha256_round_constants[1] + w1);
  ROUND(s, 2, sha256_round_constants[2] + w2);
  ROUND(s, 3, sha256_round_constants[3]);
  std::memcpy(round3_, s, sizeof round3_);
  w16_ = sigma0(w1) + w0;
  w17_ = sigma1(kHeaderBits) + sigma0(w2) + w1;
  w18_ = sigma1(w16_) + w2;
  w19_ = sigma1(w17_) + sigma0(kOne);
  w31_ = sigma0(w16_) + kHeaderBits;
  w32_ = sigma0(w17_) + w16_;
}

// The first hash of the header with this nonce, as eight words.
inline __attribute__((always_inline)) void Sha256d::first(uint32_t nonce, uint32_t out[8]) const {
  const uint32_t n = __builtin_bswap32(nonce);
  uint32_t s[8] = {round3_[0] + n, round3_[1], round3_[2], round3_[3],
                   round3_[4] + n, round3_[5], round3_[6], round3_[7]};
  uint32_t w[64];
  ROUND(s, 4, sha256_round_constants[4] + kOne);
  ROUND(s, 5, sha256_round_constants[5]);
  ROUND(s, 6, sha256_round_constants[6]);
  ROUND(s, 7, sha256_round_constants[7]);
  ROUND(s, 8, sha256_round_constants[8]);
  ROUND(s, 9, sha256_round_constants[9]);
  ROUND(s, 10, sha256_round_constants[10]);
  ROUND(s, 11, sha256_round_constants[11]);
  ROUND(s, 12, sha256_round_constants[12]);
  ROUND(s, 13, sha256_round_constants[13]);
  ROUND(s, 14, sha256_round_constants[14]);
  ROUND(s, 15, sha256_round_constants[15] + kHeaderBits);
  ROUND(s, 16, sha256_round_constants[16] + w16_);
  ROUND(s, 17, sha256_round_constants[17] + w17_);
  w[17] = w17_;
  w[18] = w18_ + sigma0(n);
  w[19] = w19_ + n;
  w[20] = sigma1(w[18]) + kOne;
  w[21] = sigma1(w[19]);
  w[22] = sigma1(w[20]) + kHeaderBits;
  w[23] = sigma1(w[21]) + w16_;
  w[24] = sigma1(w[22]) + w17_;
  w[25] = sigma1(w[23]) + w[18];
  w[26] = sigma1(w[24]) + w[19];
  w[27] = sigma1(w[25]) + w[20];
  w[28] = sigma1(w[26]) + w[21];
  w[29] = sigma1(w[27]) + w[22];
  w[30] = sigma1(w[28]) + w[23] + sigma0(kHeaderBits);
  w[31] = sigma1(w[29]) + w[24] + w31_;
  w[32] = sigma1(w[30]) + w[25] + w32_;
  STEP(s, w, 18);
  STEP(s, w, 19);
  STEP4(s, w, 20);
  STEP4(s, w, 24);
  STEP4(s, w, 28);
  STEP(s, w, 32);
  NEXT(s, w, 33);
  NEXT(s, w, 34);
  NEXT(s, w, 35);
  NEXT4(s, w, 36);
  NEXT4(s, w, 40);
  NEXT4(s, w, 44);
  NEXT4(s, w, 48);
  NEXT4(s, w, 52);
  NEXT4(s, w, 56);
  NEXT4(s, w, 60);

  out[0] = mid_[0] + s[0];
  out[1] = mid_[1] + s[1];
  out[2] = mid_[2] + s[2];
  out[3] = mid_[3] + s[3];
  out[4] = mid_[4] + s[4];
  out[5] = mid_[5] + s[5];
  out[6] = mid_[6] + s[6];
  out[7] = mid_[7] + s[7];
}

uint32_t Sha256d::top(uint32_t nonce) const {
  uint32_t w[64];
  first(nonce, w);
  uint32_t u[8] = {sha256_initial_words[0], sha256_initial_words[1], sha256_initial_words[2], sha256_initial_words[3], sha256_initial_words[4], sha256_initial_words[5], sha256_initial_words[6], sha256_initial_words[7]};
  STEP4(u, w, 0);
  STEP4(u, w, 4);
  ROUND(u, 8, sha256_round_constants[8] + kOne);
  ROUND(u, 9, sha256_round_constants[9]);
  ROUND(u, 10, sha256_round_constants[10]);
  ROUND(u, 11, sha256_round_constants[11]);
  ROUND(u, 12, sha256_round_constants[12]);
  ROUND(u, 13, sha256_round_constants[13]);
  ROUND(u, 14, sha256_round_constants[14]);
  ROUND(u, 15, sha256_round_constants[15] + kHashBits);
  w[16] = sigma0(w[1]) + w[0];
  w[17] = sigma1(kHashBits) + sigma0(w[2]) + w[1];
  w[18] = sigma1(w[16]) + sigma0(w[3]) + w[2];
  w[19] = sigma1(w[17]) + sigma0(w[4]) + w[3];
  w[20] = sigma1(w[18]) + sigma0(w[5]) + w[4];
  w[21] = sigma1(w[19]) + sigma0(w[6]) + w[5];
  w[22] = sigma1(w[20]) + kHashBits + sigma0(w[7]) + w[6];
  w[23] = sigma1(w[21]) + w[16] + sigma0(kOne) + w[7];
  w[24] = sigma1(w[22]) + w[17] + kOne;
  w[25] = sigma1(w[23]) + w[18];
  w[26] = sigma1(w[24]) + w[19];
  w[27] = sigma1(w[25]) + w[20];
  w[28] = sigma1(w[26]) + w[21];
  w[29] = sigma1(w[27]) + w[22];
  w[30] = sigma1(w[28]) + w[23] + sigma0(kHashBits);
  w[31] = sigma1(w[29]) + w[24] + sigma0(w[16]) + kHashBits;
  STEP4(u, w, 16);
  STEP4(u, w, 20);
  STEP4(u, w, 24);
  STEP4(u, w, 28);
  NEXT4(u, w, 32);
  NEXT4(u, w, 36);
  NEXT4(u, w, 40);
  NEXT4(u, w, 44);
  NEXT4(u, w, 48);
  NEXT4(u, w, 52);
  NEXT(u, w, 56);
  EXPAND(w, 57);
  ROUND_E(u, 57, sha256_round_constants[57] + w[57]);
  EXPAND(w, 58);
  ROUND_E(u, 58, sha256_round_constants[58] + w[58]);
  EXPAND(w, 59);
  ROUND_E(u, 59, sha256_round_constants[59] + w[59]);
  EXPAND(w, 60);
  ROUND_E(u, 60, sha256_round_constants[60] + w[60]);
  return __builtin_bswap32(sha256_initial_words[7] + u[7]);
}

void Sha256d::hash(uint32_t nonce, uint8_t out[32]) const {
  uint32_t words[8];
  first(nonce, words);
  uint8_t block[64] = {};
  for (int i = 0; i < 8; ++i) {
    block[i * 4] = static_cast<uint8_t>(words[i] >> 24);
    block[i * 4 + 1] = static_cast<uint8_t>(words[i] >> 16);
    block[i * 4 + 2] = static_cast<uint8_t>(words[i] >> 8);
    block[i * 4 + 3] = static_cast<uint8_t>(words[i]);
  }
  block[32] = 0x80;
  block[62] = 0x01;
  uint32_t h[8] = {sha256_initial_words[0], sha256_initial_words[1], sha256_initial_words[2], sha256_initial_words[3], sha256_initial_words[4], sha256_initial_words[5], sha256_initial_words[6], sha256_initial_words[7]};
  sha256_compress(h, block);
  for (int i = 0; i < 8; ++i) {
    out[i * 4] = static_cast<uint8_t>(h[i] >> 24);
    out[i * 4 + 1] = static_cast<uint8_t>(h[i] >> 16);
    out[i * 4 + 2] = static_cast<uint8_t>(h[i] >> 8);
    out[i * 4 + 3] = static_cast<uint8_t>(h[i]);
  }
}

}
