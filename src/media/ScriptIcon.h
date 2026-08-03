#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "core/render/Canvas.h"
#include "core/script/ScriptServices.h"

namespace awtrix {

class GifPlayer;

// Small LRU cache of decoded 8x8 icons for scripts. Scripts redraw every tick, so re-opening the
// file each time would hammer the filesystem; the cache is deliberately tiny to bound RAM.
class ScriptIcon : public script::IScriptIcon {
 public:
  ~ScriptIcon() override;

  bool draw(Canvas& canvas, const std::string& name, int x, int y, int64_t nowMs) override;

  // Drops every cached entry. Call after the icon files on flash have changed.
  void invalidate();

  void setLog(std::function<void(const std::string&)> log) { log_ = std::move(log); }

 private:
  static constexpr std::size_t kEntries = 4;
  static constexpr std::size_t kMaxNameLen = 64;

  enum class State : uint8_t {
    kGood,
    kMissing,
    kOom,
  };

  struct Entry {
    std::string name;
    Canvas buf{8, 8};
    GifPlayer* anim = nullptr;
    bool occupied = false;
    State state = State::kMissing;
    int64_t nextRetryMs = 0;
    uint8_t retryStep = 0;
    uint32_t lastUsed = 0;
  };

  Entry* acquire(const std::string& name, int64_t nowMs);
  void load(Entry& e, int64_t nowMs);
  static void release(Entry& e);

  Entry entries_[kEntries];
  uint32_t tick_ = 0;
  std::function<void(const std::string&)> log_;
};

}
