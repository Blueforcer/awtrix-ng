#include "media/ScriptIcon.h"

#include <algorithm>
#include <new>

#include "core/icons/IconSource.h"
#include "core/render/Canvas.h"
#include "media/GifPlayer.h"
#include "media/IconRenderer.h"

namespace awtrix {

namespace {

uint64_t hashOf(std::string_view text) {
  uint64_t hash = 14695981039346656037ull;
  for (const char c : text) {
    hash ^= static_cast<uint8_t>(c);
    hash *= 1099511628211ull;
  }
  return hash;
}

// An icon that failed on memory may well succeed later, so retry it on a widening backoff
// instead of writing it off. The last step repeats forever.
constexpr long kOomBackoffMs[] = {2000, 5000, 10000};
constexpr uint8_t kOomBackoffSteps = sizeof(kOomBackoffMs) / sizeof(kOomBackoffMs[0]);

constexpr int64_t kOomLogIntervalMs = 60000;

constexpr int kMaxCoordinate = 65535;

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

void ScriptIconSet::reset(Entry& e) {
  delete e.anim;
  e.anim = nullptr;
  e.width = e.height = 0;
  e.pixels.clear();
  e.state = State::kMissing;
  e.nextRetryMs = 0;
  e.retryStep = 0;
  e.resetRemote();
  e.key = 0;
  e.length = 0;
}

void ScriptIconSet::release() {
  entries_.reset();
  entryCount_ = 0;
}

// Takes over an opened GIF: a single frame becomes plain pixels, an animation keeps its player.
// False when there is no memory for the frame; the player is gone either way.
bool ScriptIconSet::adopt(Entry& e, GifPlayer* gif, int64_t nowMs) {
  const GifPlayer::Frame frame = gif->takeFrame(e.pixels);
  if (frame == GifPlayer::Frame::kOom) {
    delete gif;
    return false;
  }
  e.width = gif->width();
  e.height = gif->height();
  if (frame == GifPlayer::Frame::kStill) {
    delete gif;
    return true;
  }
  e.anim = gif;
  Canvas buf(e.width, e.height, e.pixels.data());
  gif->render(buf, nowMs);
  return true;
}

void ScriptIconSet::load(Entry& e, std::string_view icon, int64_t nowMs) {
  delete e.anim;
  e.anim = nullptr;
  e.width = e.height = 0;
  e.resetRemote();
  e.pixels.clear();

  if (icons::parse(icon).remote()) {
    loadRemote(e, icon, nowMs);
    return;
  }

  // Capped at one resident frame on purpose: several icons can be cached at once, so animated
  // ones stream rather than each holding a pile of decoded frames.
  GifPlayer* gif = new (std::nothrow) GifPlayer();
  GifPlayer::OpenResult r =
      gif ? gif->open(icon, service_.maxWidth(), service_.maxHeight(), false, 1)
          : GifPlayer::OpenResult::kOom;

  if (r == GifPlayer::OpenResult::kGood) {
    if (adopt(e, gif, nowMs)) {
      e.state = State::kGood;
      e.retryStep = 0;
      return;
    }
    r = GifPlayer::OpenResult::kOom;
  } else {
    delete gif;
  }

  if (r == GifPlayer::OpenResult::kMissing) {
    // No GIF under that name, so fall back to the JPG icon of the same name.
    if (e.pixels.resize(8 * 8)) {
      Canvas buf(8, 8, e.pixels.data());
      buf.clear();
      bool outOfMemory = false;
      if (icon::draw(buf, icon, 0, 0, &outOfMemory)) {
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

  if (r == GifPlayer::OpenResult::kOom) outOfMemory(e, icon, nowMs);
}

void ScriptIconSet::outOfMemory(Entry& e, std::string_view icon, int64_t nowMs) {
  e.state = State::kOom;
  if (e.retryStep == 0) {
    const icons::Source source = icons::parse(icon);
    service_.logOom(source.inlined() ? "data URL" : source.remote() ? "URL" : std::string(icon), nowMs);
  }
  e.nextRetryMs = nowMs + kOomBackoffMs[e.retryStep];
  if (e.retryStep + 1 < kOomBackoffSteps) ++e.retryStep;
}

ScriptIconSet::Entry* ScriptIconSet::acquire(std::string_view icon, int64_t nowMs) {
  const uint64_t key = hashOf(icon);
  for (Entry* e = entries_.get(); e; e = e->next.get()) {
    if (e->key == key && e->length == icon.size()) {
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

  victim->key = key;
  victim->length = icon.size();
  victim->lastUsedMs = nowMs;
  load(*victim, icon, nowMs);
  return victim;
}

bool ScriptIconSet::draw(Canvas& canvas, std::string_view icon, int x, int y, int64_t nowMs) {
  if (icons::parse(icon).kind == icons::Source::Kind::kInvalid) return false;
  if (canvas.width() <= 0 || canvas.height() <= 0) return false;
  if (service_.maxWidth() <= 0 || service_.maxHeight() <= 0) return false;
  if (generation_ != service_.generation()) {
    release();
    generation_ = service_.generation();
  }

  Entry* e = acquire(icon, nowMs);
  if (!e) return false;
  if ((e->state == State::kOom && nowMs >= e->nextRetryMs) ||
      (e->state == State::kPending && service_.remoteChanged(*e)))
    load(*e, icon, nowMs);
  if (e->state != State::kGood) return false;
  x = std::clamp(x, -kMaxCoordinate, kMaxCoordinate) + e->offsetX();
  y = std::clamp(y, -kMaxCoordinate, kMaxCoordinate) + e->offsetY();

  if (e->anim) {
    Canvas buf(e->width, e->height, e->pixels.data());
    e->anim->render(buf, nowMs);
  }

  for (int row = 0; row < e->height; ++row)
    for (int col = 0; col < e->width; ++col)
      canvas.setPixel(x + col, y + row, e->pixels[static_cast<size_t>(row) * e->width + col]);
  return true;
}

}
