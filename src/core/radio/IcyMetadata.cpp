#include "core/radio/IcyMetadata.h"

#include <cctype>

#include "core/render/TextEncoding.h"

namespace awtrix {
namespace radio {

namespace {

constexpr char kKey[] = "StreamTitle=";
constexpr std::size_t kKeyLength = sizeof(kKey) - 1;

// Track titles legitimately contain "';", so a candidate terminator only counts when what follows
// is the end of the block, the NUL padding, or another Key='...' field.
bool startsNextField(const std::string& block, std::size_t at) {
  if (at >= block.size()) return true;
  if (block[at] == '\0') return true;

  std::size_t i = at;
  if (!std::isalpha(static_cast<unsigned char>(block[i]))) return false;
  while (i < block.size() &&
         (std::isalnum(static_cast<unsigned char>(block[i])) || block[i] == '_'))
    ++i;
  return i + 1 < block.size() && block[i] == '=' && block[i + 1] == '\'';
}

}

bool parseStreamTitle(const std::string& block, std::string& title) {
  const std::size_t key = block.find(kKey);
  if (key == std::string::npos) return false;

  std::size_t open = key + kKeyLength;
  if (open >= block.size() || block[open] != '\'') return false;
  ++open;

  std::size_t close = std::string::npos;
  for (std::size_t candidate = block.find("';", open); candidate != std::string::npos;
       candidate = block.find("';", candidate + 1)) {
    if (startsNextField(block, candidate + 2)) {
      close = candidate;
      break;
    }
  }

  // Some stations never close the field; fall back to the NUL padding that pads the block out to
  // its 16-byte multiple.
  if (close == std::string::npos) {
    close = block.find('\0', open);
    if (close == std::string::npos) close = block.size();
    if (close > open && block[close - 1] == '\'') --close;
  }

  title.assign(block, open, close - open);
  return true;
}

bool TitleTracker::update(const std::string& block) {
  std::string candidate;
  if (!parseStreamTitle(block, candidate)) return false;
  candidate = text::fromStreamBytes(candidate);
  if (candidate == title_) return false;
  title_ = candidate;
  return true;
}

}
}
