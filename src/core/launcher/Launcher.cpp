#include "core/launcher/Launcher.h"

#include <algorithm>

#include "core/Command.h"
#include "core/CoreEngine.h"
#include "core/StrCase.h"
#include "core/sound/AudioRouter.h"

namespace awtrix::launcher {

void Launcher::open(int64_t nowMs) {
  open_ = true;
  openedAtMs_ = nowMs;
  closedAtMs_ = -1;
  notice_.clear();
  show(Page::Root, nowMs);
  if (entries_.empty()) tell("Empty", nowMs);
}

void Launcher::close(int64_t nowMs) {
  if (!open_) return;
  open_ = false;
  closedAtMs_ = nowMs;
}

void Launcher::step(int direction, int64_t nowMs) {
  lastInputMs_ = nowMs;
  const int count = static_cast<int>(entries_.size());
  if (!open_ || count < 2 || !notice_.empty()) return;
  previousSelected_ = selected_;
  selected_ = ((selected_ + direction) % count + count) % count;
  moveDirection_ = direction < 0 ? -1 : 1;
  movedAtMs_ = nowMs;
  pageChanged_ = false;
}

void Launcher::confirm(int64_t nowMs) {
  lastInputMs_ = nowMs;
  if (!open_ || !notice_.empty() || entries_.empty()) return;
  const Entry entry = entries_[selected_];
  switch (entry.kind) {
    case Entry::Kind::Scripts:
      show(Page::Scripts, nowMs);
      break;
    case Entry::Kind::Radio:
      show(Page::Radio, nowMs);
      break;
    case Entry::Kind::Script: {
      DispatchDetail detail;
      if (engine_.startSession(entry.script, detail) == DispatchResult::Ok)
        close(nowMs);
      else
        tell("Failed", nowMs);
      break;
    }
    case Entry::Kind::Station: {
      Command play(CommandType::PlayAudio);
      play.source = Source::Menu;
      play.payload = "{\"station\":" + std::to_string(entry.station) + "}";
      engine_.execute(play);
      refreshRadio();
      break;
    }
    case Entry::Kind::Stop: {
      Command stop(CommandType::StopAudio);
      stop.source = Source::Menu;
      stop.arg = static_cast<int>(sound::Stop::Radio);
      engine_.execute(stop);
      refreshRadio();
      break;
    }
  }
}

void Launcher::tick(int64_t nowMs) {
  if (!open_) return;
  if (engine_.state().runtime().matrixOff) {
    close(nowMs);
    return;
  }
  if (!notice_.empty() && nowMs >= noticeUntilMs_) {
    notice_.clear();
    if (entries_.empty()) close(nowMs);
  }
  if (open_ && nowMs - lastInputMs_ >= kIdleCloseMs) close(nowMs);
  if (open_ && page_ == Page::Radio) refreshRadio();
}

void Launcher::show(Page page, int64_t nowMs) {
  page_ = page;
  switch (page) {
    case Page::Root: entries_ = rootEntries(); break;
    case Page::Scripts: entries_ = scriptEntries(); break;
    case Page::Radio: entries_ = radioEntries(); break;
  }
  const RuntimeState& runtime = engine_.state().runtime();
  radioPlaying_ = runtime.radioPlaying;
  radioStation_ = runtime.radioStation;
  previousSelected_ = selected_;
  selected_ = 0;
  // A playing station opens under the cursor, one past Stop.
  if (page == Page::Radio)
    for (int i = 0; i < static_cast<int>(entries_.size()); ++i)
      if (entries_[i].playing) selected_ = i;
  moveDirection_ = 1;
  movedAtMs_ = nowMs;
  pageChanged_ = true;
  lastInputMs_ = nowMs;
}

// Rebuilds the station list when the stream changed, keeping the cursor on the same entry.
void Launcher::refreshRadio() {
  const RuntimeState& runtime = engine_.state().runtime();
  if (runtime.radioPlaying == radioPlaying_ && runtime.radioStation == radioStation_) return;
  radioPlaying_ = runtime.radioPlaying;
  radioStation_ = runtime.radioStation;
  const Entry current = entries_.empty() ? Entry{} : entries_[selected_];
  entries_ = radioEntries();
  selected_ = 0;
  for (int i = 0; i < static_cast<int>(entries_.size()); ++i)
    if (entries_[i].sameAs(current)) selected_ = i;
  previousSelected_ = selected_;
}

std::vector<Entry> Launcher::rootEntries() const {
  std::vector<Entry> out;
  if (!engine_.onDemandApps().empty()) {
    Entry scripts;
    scripts.kind = Entry::Kind::Scripts;
    scripts.label = "Scripts";
    out.push_back(scripts);
  }
  if (engine_.radioAvailable() && !engine_.stations().empty()) {
    Entry radio;
    radio.kind = Entry::Kind::Radio;
    radio.label = "Radio";
    radio.playing = engine_.state().runtime().radioPlaying;
    out.push_back(radio);
  }
  return out;
}

std::vector<Entry> Launcher::scriptEntries() const {
  std::vector<Entry> out;
  IScriptService* scripts = engine_.scriptService();
  for (const std::string& name : engine_.onDemandApps()) {
    Entry entry;
    entry.kind = Entry::Kind::Script;
    entry.script = name;
    entry.label = scripts ? scripts->scriptTitle(name) : name;
    out.push_back(entry);
  }
  std::sort(out.begin(), out.end(),
            [](const Entry& x, const Entry& y) { return strcase::alphaLess(x.label, y.label); });
  return out;
}

std::vector<Entry> Launcher::radioEntries() const {
  std::vector<Entry> out;
  const RuntimeState& runtime = engine_.state().runtime();
  if (runtime.radioPlaying) {
    Entry stop;
    stop.kind = Entry::Kind::Stop;
    stop.label = "Stop";
    out.push_back(stop);
  }
  const auto& stations = engine_.stations();
  for (int i = 0; i < static_cast<int>(stations.size()); ++i) {
    Entry entry;
    entry.kind = Entry::Kind::Station;
    entry.station = i;
    entry.label = stations[i].name;
    entry.playing = runtime.radioPlaying && runtime.radioStation == stations[i].name;
    out.push_back(entry);
  }
  return out;
}

void Launcher::tell(const std::string& text, int64_t nowMs) {
  notice_ = text;
  noticeUntilMs_ = nowMs + kNoticeMs;
}

}
