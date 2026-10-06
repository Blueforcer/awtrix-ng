#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <string>

#include "core/synth/Song.h"
#include "core/synth/SongParser.h"

namespace awtrix {
namespace synth {

// Parsed songs by their text, so a game that plays the same effect over and over parses it once,
// and the same text always yields the same Song - which is how a player knows a song asked for
// again is the one it is playing. Not thread-safe: one thread asks.
class SongCache {
 public:
  explicit SongCache(std::size_t budgetBytes = kDefaultBudget) : budget_(budgetBytes) {}

  // The song for text, parsed now or from the cache; a failed parse is not kept.
  ParseResult get(const std::string& text);
  std::size_t bytes() const { return bytes_; }
  std::size_t size() const { return entries_.size(); }

  static constexpr std::size_t kDefaultBudget = 512 * 1024;

 private:
  struct Entry {
    std::string text;
    std::shared_ptr<const Song> song;
    std::size_t bytes = 0;
  };

  std::size_t budget_;
  std::size_t bytes_ = 0;
  // Most recently used first.
  std::list<Entry> entries_;
};

}
}
