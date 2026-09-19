#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "core/script/ScriptServices.h"
#include "media/PodBuffer.h"

namespace awtrix {

class GifPlayer;
class ScriptIcon;

// One script app's icons. Entries are allocated only for used icons, up to 4 IDs. Icons drawn
// at the same nowMs stay resident together; a fifth ID in that draw tick returns false
// instead of evicting one already drawn and restarting its animation. Older entries are evicted
// least-recently used, and release() drops them all.
class ScriptIconSet : public script::IScriptIconSet {
 public:
  explicit ScriptIconSet(ScriptIcon& service);
  ~ScriptIconSet() override;

  bool draw(Canvas& canvas, std::string_view name, int x, int y, int64_t nowMs) override;
  void release() override;

 private:
  static constexpr std::size_t kMaxEntries = 4;
  static constexpr std::size_t kMaxNameLen = 64;

  enum class State : uint8_t {
    kGood,
    kMissing,
    kOom,
  };

  struct Entry {
    ~Entry();

    char name[kMaxNameLen + 1] = {};
    media::PodBuffer<uint32_t> pixels;
    int width = 0;
    int height = 0;
    GifPlayer* anim = nullptr;
    State state = State::kMissing;
    int64_t nextRetryMs = 0;
    uint8_t retryStep = 0;
    int64_t lastUsedMs = 0;
    std::unique_ptr<Entry> next;
  };

  Entry* acquire(std::string_view name, int64_t nowMs);
  void load(Entry& e, int64_t nowMs);
  static void reset(Entry& e);

  ScriptIcon& service_;
  std::unique_ptr<Entry> entries_;
  std::size_t entryCount_ = 0;
  uint32_t generation_ = 0;
};

class ScriptIcon : public script::IScriptIcon {
 public:
  std::unique_ptr<script::IScriptIconSet> createSet() override;

  // Makes every set drop its entries on its next draw. Call after the icon files on flash have
  // changed.
  void invalidate() { ++generation_; }
  void setPanelSize(int width, int height);

  void setLog(std::function<void(const std::string&)> log) { log_ = std::move(log); }

  int maxWidth() const { return maxWidth_; }
  int maxHeight() const { return maxHeight_; }
  uint32_t generation() const { return generation_; }
  void logOom(const std::string& name, int64_t nowMs);

 private:
  int maxWidth_ = 0;
  int maxHeight_ = 0;
  uint32_t generation_ = 0;
  bool oomLogged_ = false;
  int64_t lastOomLogMs_ = 0;
  std::function<void(const std::string&)> log_;
};

}
