#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "platform/tc002/speech/SpeechTypes.h"

namespace awtrix::speech {

// A model input of token contract v1: the phone, its stress (0 none, 1 primary, 2 secondary),
// whether it ends a word, and on a pause the punctuation that ended the phrase (0 none, 1 ','
// 2 '.' 3 '?' 4 '!' 5 ';' 6 ':', 7 the BOS token that starts every chunk).
struct Token {
  uint8_t phone = 0;
  uint8_t stress = 0;
  uint8_t wordEnd = 0;
  uint8_t punctuation = 0;
};
constexpr uint8_t kBos = 7;

// Phones [begin, end) of a plan, spoken as one model input.
struct Chunk {
  std::size_t begin = 0;
  std::size_t end = 0;
};

constexpr std::size_t kMaxChunkPhones = 127;
constexpr std::size_t kMinChunkPhones = 8;

// Whether every phone and stress of the plan has a token.
bool tokenizable(const Plan& plan);
// A chunk per sentence. A sentence longer than kMaxChunkPhones is cut after its last pause within
// the limit, a phrase that long after its last whole word. A sentence or piece shorter than
// kMinChunkPhones joins its neighbour while the two fit into one chunk.
void splitPlan(const Plan& plan, std::vector<Chunk>& out);
// BOS, then one token per phone of the chunk; returns the count, chunk length + 1.
std::size_t chunkTokens(const Plan& plan, Chunk chunk, Token* out);

}
