#include "sha256.h"

#include <string.h>

static uint32_t rotate_right(uint32_t value, unsigned bits) {
  return (value >> bits) | (value << (32 - bits));
}

void sha256_compress(uint32_t words[8], const uint8_t block[64]) {
  uint32_t w[64];
  for (int i = 0; i < 16; ++i)
    w[i] = (uint32_t)block[i * 4] << 24 | (uint32_t)block[i * 4 + 1] << 16 |
           (uint32_t)block[i * 4 + 2] << 8 | (uint32_t)block[i * 4 + 3];
  for (int i = 16; i < 64; ++i) {
    const uint32_t s0 = rotate_right(w[i - 15], 7) ^ rotate_right(w[i - 15], 18) ^ (w[i - 15] >> 3);
    const uint32_t s1 = rotate_right(w[i - 2], 17) ^ rotate_right(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  uint32_t a = words[0], b = words[1], c = words[2], d = words[3];
  uint32_t e = words[4], f = words[5], g = words[6], h = words[7];
  for (int i = 0; i < 64; ++i) {
    const uint32_t t1 = h + (rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25)) +
                        ((e & f) ^ (~e & g)) + sha256_round_constants[i] + w[i];
    const uint32_t t2 = (rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22)) +
                        ((a & b) ^ (a & c) ^ (b & c));
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }
  words[0] += a;
  words[1] += b;
  words[2] += c;
  words[3] += d;
  words[4] += e;
  words[5] += f;
  words[6] += g;
  words[7] += h;
}

void sha256_init(struct sha256_state* state) {
  memcpy(state->h, sha256_initial_words, sizeof sha256_initial_words);
  state->length = 0;
  state->used = 0;
}

void sha256_update(struct sha256_state* state, const void* data, size_t length) {
  const uint8_t* bytes = (const uint8_t*)data;
  state->length += length;
  if (state->used) {
    const size_t take = length < 64 - state->used ? length : 64 - state->used;
    memcpy(state->block + state->used, bytes, take);
    state->used += take;
    bytes += take;
    length -= take;
    if (state->used < 64) return;
    sha256_compress(state->h, state->block);
    state->used = 0;
  }
  while (length >= 64) {
    sha256_compress(state->h, bytes);
    bytes += 64;
    length -= 64;
  }
  memcpy(state->block, bytes, length);
  state->used = length;
}

void sha256_final(struct sha256_state* state, uint8_t digest[32]) {
  const uint64_t bits = state->length * 8;
  state->block[state->used++] = 0x80;
  if (state->used > 56) {
    memset(state->block + state->used, 0, 64 - state->used);
    sha256_compress(state->h, state->block);
    state->used = 0;
  }
  memset(state->block + state->used, 0, 56 - state->used);
  for (int i = 0; i < 8; ++i) state->block[56 + i] = (uint8_t)(bits >> (56 - 8 * i));
  sha256_compress(state->h, state->block);
  for (int i = 0; i < 8; ++i) {
    digest[i * 4] = (uint8_t)(state->h[i] >> 24);
    digest[i * 4 + 1] = (uint8_t)(state->h[i] >> 16);
    digest[i * 4 + 2] = (uint8_t)(state->h[i] >> 8);
    digest[i * 4 + 3] = (uint8_t)state->h[i];
  }
}

void sha256_hex(const uint8_t digest[32], char hex[65]) {
  static const char kDigits[] = "0123456789abcdef";
  for (int i = 0; i < 32; ++i) {
    hex[i * 2] = kDigits[digest[i] >> 4];
    hex[i * 2 + 1] = kDigits[digest[i] & 15];
  }
  hex[64] = '\0';
}
