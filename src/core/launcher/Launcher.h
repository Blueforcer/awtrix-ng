#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/input/ButtonRouter.h"

namespace awtrix {

class CoreEngine;

namespace launcher {

enum class Page : uint8_t { Root, Scripts, Radio };

struct Entry {
  enum class Kind : uint8_t { Scripts, Radio, Script, Station, Stop };
  Kind kind = Kind::Scripts;
  std::string label;
  std::string script;
  int station = -1;
  bool playing = false;

  bool sameAs(const Entry& other) const {
    return kind == other.kind && script == other.script && station == other.station;
  }
};

// The device menu behind a held select: pick an @ondemand script to run on its own, or a radio
// station. It only decides; LauncherView draws it and ButtonRouter drives it.
class Launcher : public input::IButtonMenu {
 public:
  static constexpr long kIdleCloseMs = 20000;
  static constexpr long kNoticeMs = 1500;

  explicit Launcher(CoreEngine& engine) : engine_(engine) {}

  bool isOpen() const override { return open_; }
  void open(int64_t nowMs) override;
  void close(int64_t nowMs) override;
  void step(int direction, int64_t nowMs) override;
  void confirm(int64_t nowMs) override;
  // Once per frame: closes an idle menu and follows the radio while its page is up.
  void tick(int64_t nowMs);

  Page page() const { return page_; }
  const std::vector<Entry>& entries() const { return entries_; }
  int selected() const { return selected_; }
  // A short message in place of the entries, such as a script that would not start.
  const std::string& notice() const { return notice_; }

  // When the menu opened and closed, and the last move, for the view's animations.
  int64_t openedAtMs() const { return openedAtMs_; }
  int64_t closedAtMs() const { return closedAtMs_; }
  int64_t movedAtMs() const { return movedAtMs_; }
  int moveDirection() const { return moveDirection_; }
  int previousSelected() const { return previousSelected_; }
  bool pageChanged() const { return pageChanged_; }

 private:
  void show(Page page, int64_t nowMs);
  void refreshRadio();
  std::vector<Entry> rootEntries() const;
  std::vector<Entry> scriptEntries() const;
  std::vector<Entry> radioEntries() const;
  void tell(const std::string& text, int64_t nowMs);

  CoreEngine& engine_;
  bool open_ = false;
  Page page_ = Page::Root;
  std::vector<Entry> entries_;
  int selected_ = 0;
  std::string notice_;
  int64_t noticeUntilMs_ = 0;
  int64_t openedAtMs_ = 0;
  int64_t closedAtMs_ = -1;
  int64_t movedAtMs_ = -1;
  int64_t lastInputMs_ = 0;
  int moveDirection_ = 0;
  int previousSelected_ = 0;
  bool pageChanged_ = false;
  bool radioPlaying_ = false;
  std::string radioStation_;
};

}

}
