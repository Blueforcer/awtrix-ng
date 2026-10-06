#include "platform/tc002/speech/SpeechTokens.h"

#include "platform/tc002/speech/SpeechModel.h"

namespace awtrix::speech {
namespace {

static_assert(static_cast<std::size_t>(PhoneId::J) + 1 == SpeechModel::kPhones,
              "a new phone needs a new token contract");
static_assert(Primary == 1 && Secondary == 2 && WordEnd == 4, "contract v1 reads stress from bits 0-1");

uint8_t punctuation(char c) {
  switch (c) {
    case ',': return 1;
    case '.': return 2;
    case '?': return 3;
    case '!': return 4;
    case ';': return 5;
    case ':': return 6;
    default: return 0;
  }
}

bool pause(const Plan& plan, std::size_t i) { return plan.phones[i].sound == PhoneId::Silence; }

bool sentenceEnd(const Plan& plan, std::size_t i) {
  const char c = plan.boundaries[i];
  return pause(plan, i) && (c == '.' || c == '?' || c == '!');
}

}

bool tokenizable(const Plan& plan) {
  for (std::size_t i = 0; i < plan.count; ++i) {
    const Phone& phone = plan.phones[i];
    if (static_cast<std::size_t>(phone.sound) >= SpeechModel::kPhones ||
        (phone.prosody & 3u) >= SpeechModel::kStresses)
      return false;
  }
  return true;
}

void splitPlan(const Plan& plan, std::vector<Chunk>& out) {
  out.clear();
  Chunk current;
  const auto add = [&](Chunk unit) {
    const std::size_t had = current.end - current.begin, adds = unit.end - unit.begin;
    if (had && (had < kMinChunkPhones || adds < kMinChunkPhones) && had + adds <= kMaxChunkPhones) {
      current.end = unit.end;
      return;
    }
    if (had) out.push_back(current);
    current = unit;
  };
  for (std::size_t start = 0; start < plan.count;) {
    std::size_t end = start;
    while (end + 1 < plan.count && !sentenceEnd(plan, end)) ++end;
    ++end;
    while (end - start > kMaxChunkPhones) {
      std::size_t afterPause = 0, afterWord = 0;
      for (std::size_t i = start; i < start + kMaxChunkPhones; ++i) {
        if (pause(plan, i)) afterPause = i + 1;
        else if (plan.phones[i].prosody & WordEnd) afterWord = i + 1;
      }
      const std::size_t cut = afterPause ? afterPause : afterWord ? afterWord : start + kMaxChunkPhones;
      add({start, cut});
      start = cut;
    }
    add({start, end});
    start = end;
  }
  if (current.end > current.begin) out.push_back(current);
}

std::size_t chunkTokens(const Plan& plan, Chunk chunk, Token* out) {
  out[0] = {0, 0, 0, kBos};
  for (std::size_t i = chunk.begin; i < chunk.end; ++i) {
    const Phone& phone = plan.phones[i];
    out[1 + i - chunk.begin] = {static_cast<uint8_t>(phone.sound), static_cast<uint8_t>(phone.prosody & 3u),
                                static_cast<uint8_t>(phone.prosody & WordEnd ? 1 : 0),
                                pause(plan, i) ? punctuation(plan.boundaries[i]) : uint8_t{0}};
  }
  return chunk.end - chunk.begin + 1;
}

}
