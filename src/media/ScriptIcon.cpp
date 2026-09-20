#include "media/ScriptIcon.h"

#include <algorithm>
#include <cstring>
#include <new>

#include "core/render/Canvas.h"
#include "media/GifPlayer.h"
#include "media/IconRenderer.h"

namespace awtrix {

namespace {

bool nameIsSafe(std::string_view name) {
  if (name.empty()) return false;
  if (name.find('\0') != std::string_view::npos) return false;
  if (name.find('/') != std::string_view::npos) return false;
  if (name.find('\\') != std::string_view::npos) return false;
  return name.find("..") == std::string_view::npos;
}

// An icon that failed on memory may well succeed later, so retry it on a widening backoff
// instead of writing it off. The last step repeats forever.
constexpr long kOomBackoffMs[] = {2000, 5000, 10000};
constexpr uint8_t kOomBackoffSteps = sizeof(kOomBackoffMs) / sizeof(kOomBackoffMs[0]);

constexpr int64_t kOomLogIntervalMs = 60000;

constexpr int kMaxCoordinate = 65535;

constexpr std::size_t kBase64PrefixLen = sizeof("base64:") - 1;

// Draws the common pixels/anim content of either cache entry kind onto the script canvas.
template <class T>
bool blitEntry(Canvas& canvas, T& e, int x, int y, int64_t nowMs) {
  x = std::clamp(x, -kMaxCoordinate, kMaxCoordinate);
  y = std::clamp(y, -kMaxCoordinate, kMaxCoordinate);

  if (e.anim) {
    Canvas buf(e.width, e.height, e.pixels.data());
    e.anim->render(buf, nowMs);
  }

  for (int row = 0; row < e.height; ++row)
    for (int col = 0; col < e.width; ++col)
      canvas.setPixel(x + col, y + row, e.pixels[static_cast<size_t>(row) * e.width + col]);
  return true;
}

}

std::unique_ptr<script::IScriptIconSet> ScriptIcon::createSet() {
  return std::unique_ptr<script::IScriptIconSet>(new (std::nothrow) ScriptIconSet(*this));
}

void ScriptIcon::setPanelSize(int width, int height) {
  if (maxWidth_ == width && maxHeight_ == height) return;
  maxWidth_ = width;
  maxHeight_ = height;
  ++generation_;
}

void ScriptIcon::logOom(const std::string& name, int64_t nowMs) {
  if (!log_) return;
  if (oomLogged_ && nowMs - lastOomLogMs_ < kOomLogIntervalMs) return;
  oomLogged_ = true;
  lastOomLogMs_ = nowMs;
  log_("icon '" + name + "': decode failed, out of memory - will retry");
}

ScriptIconSet::ScriptIconSet(ScriptIcon& service)
    : service_(service), generation_(service.generation()) {}

ScriptIconSet::~ScriptIconSet() = default;

ScriptIconSet::Entry::~Entry() { delete anim; }

ScriptIconSet::InlineEntry::~InlineEntry() { delete anim; }

void ScriptIconSet::reset(Entry& e) {
  delete e.anim;
  e.anim = nullptr;
  e.width = e.height = 0;
  e.pixels.clear();
  e.state = State::kMissing;
  e.nextRetryMs = 0;
  e.retryStep = 0;
  e.name[0] = '\0';
}

void ScriptIconSet::release() {
  entries_.reset();
  entryCount_ = 0;
  inlineEntries_.reset();
  inlineEntryCount_ = 0;
}

void ScriptIconSet::resetInline(InlineEntry& e) {
  delete e.anim;
  e.anim = nullptr;
  e.width = e.height = 0;
  e.pixels.clear();
  e.state = State::kMissing;
  e.nextRetryMs = 0;
  e.retryStep = 0;
  e.name.clear();
}

void ScriptIconSet::load(Entry& e, int64_t nowMs) {
  loadShared(e, std::string(e.name), nowMs);
}

void ScriptIconSet::loadInline(InlineEntry& e, int64_t nowMs) {
  loadShared(e, e.name, nowMs);
}

template <class T>
void ScriptIconSet::loadShared(T& e, const std::string& name, int64_t nowMs) {
  delete e.anim;
  e.anim = nullptr;
  e.width = e.height = 0;
  e.pixels.clear();

  // Capped at one resident frame on purpose: several icons can be cached at once, so animated
  // ones stream rather than each holding a pile of decoded frames.
  GifPlayer* gif = new (std::nothrow) GifPlayer();
  GifPlayer::OpenResult r =
      gif ? gif->open(name, service_.maxWidth(), service_.maxHeight(), false, 1)
          : GifPlayer::OpenResult::kOom;

  if (r == GifPlayer::OpenResult::kGood) {
    const int width = gif->width();
    const int height = gif->height();
    if (gif->takeStaticFrame(e.pixels)) {
      e.width = width;
      e.height = height;
      delete gif;
      e.state = State::kGood;
      e.retryStep = 0;
      return;
    }
    const bool transferred = gif->takeInitialFrame(e.pixels);
    if (transferred || e.pixels.resize(static_cast<size_t>(gif->width()) * gif->height())) {
      e.anim = gif;
      e.width = width;
      e.height = height;
      Canvas buf(width, height, e.pixels.data());
      if (!transferred) buf.clear();
      gif->render(buf, nowMs);
      e.state = State::kGood;
      e.retryStep = 0;
      return;
    }
    r = GifPlayer::OpenResult::kOom;
  }
  delete gif;

  if (r == GifPlayer::OpenResult::kMissing) {
    // No GIF under that name, so fall back to the JPG icon of the same id.
    if (e.pixels.resize(8 * 8)) {
      Canvas buf(8, 8, e.pixels.data());
      buf.clear();
      bool outOfMemory = false;
      if (icon::draw(buf, name, 0, 0, &outOfMemory)) {
        e.width = e.height = 8;
        e.state = State::kGood;
        e.retryStep = 0;
        return;
      }
      e.pixels.clear();
      if (!outOfMemory) {
        e.state = State::kMissing;
        e.retryStep = 0;
        return;
      }
    }
    r = GifPlayer::OpenResult::kOom;
  }

  if (r == GifPlayer::OpenResult::kOom) {
    e.state = State::kOom;
    if (e.retryStep == 0) service_.logOom(name, nowMs);
    e.nextRetryMs = nowMs + kOomBackoffMs[e.retryStep];
    if (e.retryStep + 1 < kOomBackoffSteps) ++e.retryStep;
  }
}

ScriptIconSet::Entry* ScriptIconSet::acquire(std::string_view name, int64_t nowMs) {
  for (Entry* e = entries_.get(); e; e = e->next.get()) {
    if (name == e->name) {
      e->lastUsedMs = nowMs;
      return e;
    }
  }

  Entry* victim = nullptr;
  if (entryCount_ < kMaxEntries) {
    std::unique_ptr<Entry> fresh(new (std::nothrow) Entry());
    if (!fresh) return nullptr;
    victim = fresh.get();
    fresh->next = std::move(entries_);
    entries_ = std::move(fresh);
    ++entryCount_;
  } else {
    for (Entry* e = entries_.get(); e; e = e->next.get()) {
      if (e->lastUsedMs != nowMs && (!victim || e->lastUsedMs < victim->lastUsedMs))
        victim = e;
    }
    if (!victim) return nullptr;
    reset(*victim);
  }

  std::memcpy(victim->name, name.data(), name.size());
  victim->name[name.size()] = '\0';
  victim->lastUsedMs = nowMs;
  load(*victim, nowMs);
  return victim;
}

ScriptIconSet::InlineEntry* ScriptIconSet::acquireInline(std::string_view name, int64_t nowMs) {
  for (InlineEntry* e = inlineEntries_.get(); e; e = e->next.get()) {
    if (name == e->name) {
      e->lastUsedMs = nowMs;
      return e;
    }
  }

  InlineEntry* victim = nullptr;
  if (inlineEntryCount_ < kMaxInlineEntries) {
    std::unique_ptr<InlineEntry> fresh(new (std::nothrow) InlineEntry());
    if (!fresh) return nullptr;
    victim = fresh.get();
    fresh->next = std::move(inlineEntries_);
    inlineEntries_ = std::move(fresh);
    ++inlineEntryCount_;
  } else {
    for (InlineEntry* e = inlineEntries_.get(); e; e = e->next.get()) {
      if (e->lastUsedMs != nowMs && (!victim || e->lastUsedMs < victim->lastUsedMs))
        victim = e;
    }
    if (!victim) return nullptr;
    resetInline(*victim);
  }

  victim->name.assign(name);
  victim->lastUsedMs = nowMs;
  loadInline(*victim, nowMs);
  return victim;
}

bool ScriptIconSet::draw(Canvas& canvas, std::string_view name, int x, int y, int64_t nowMs) {
  if (canvas.width() <= 0 || canvas.height() <= 0) return false;
  if (service_.maxWidth() <= 0 || service_.maxHeight() <= 0) return false;
  if (generation_ != service_.generation()) {
    release();
    generation_ = service_.generation();
  }

  // base64: prefixed names carry the image bytes and can be far longer than the 64-byte entry
  // name slot, so they are routed to a dedicated inline cache instead of the named one.
  if (name.rfind("base64:", 0) == 0) return drawInline(canvas, name, x, y, nowMs);

  if (!nameIsSafe(name) || name.size() > kMaxNameLen) return false;

  Entry* e = acquire(name, nowMs);
  if (!e) return false;
  if (e->state == State::kOom && nowMs >= e->nextRetryMs) load(*e, nowMs);
  if (e->state != State::kGood) return false;
  return blitEntry(canvas, *e, x, y, nowMs);
}

bool ScriptIconSet::drawInline(Canvas& canvas, std::string_view name, int x, int y,
                               int64_t nowMs) {
  if (name.size() > kBase64PrefixLen + kMaxInlineBase64) return false;

  InlineEntry* e = acquireInline(name, nowMs);
  if (!e) return false;
  if (e->state == State::kOom && nowMs >= e->nextRetryMs) loadInline(*e, nowMs);
  if (e->state != State::kGood) return false;
  return blitEntry(canvas, *e, x, y, nowMs);
}

}
