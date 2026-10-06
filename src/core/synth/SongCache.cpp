#include "core/synth/SongCache.h"

namespace awtrix {
namespace synth {

ParseResult SongCache::get(const std::string& text) {
  for (auto it = entries_.begin(); it != entries_.end(); ++it) {
    if (it->text != text) continue;
    entries_.splice(entries_.begin(), entries_, it);
    ParseResult hit;
    hit.song = entries_.front().song;
    return hit;
  }
  ParseResult parsed = parse(text);
  if (!parsed.ok()) return parsed;
  Entry entry;
  entry.text = text;
  entry.song = parsed.song;
  entry.bytes = text.size() + parsed.song->bytes();
  if (entry.bytes > budget_) return parsed;
  bytes_ += entry.bytes;
  entries_.push_front(std::move(entry));
  while (bytes_ > budget_) {
    bytes_ -= entries_.back().bytes;
    entries_.pop_back();
  }
  return parsed;
}

}
}
