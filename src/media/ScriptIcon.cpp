#include "media/ScriptIcon.h"

#include <new>

#include "media/GifPlayer.h"
#include "media/IconRenderer.h"

namespace awtrix {

namespace {

bool nameIsSafe(const std::string& name) {
  if (name.empty()) return false;
  if (name.find('/') != std::string::npos) return false;
  if (name.find('\\') != std::string::npos) return false;
  return name.find("..") == std::string::npos;
}

// An icon that failed on memory may well succeed later, so retry it on a widening backoff
// instead of writing it off. The last step repeats forever.
constexpr long kOomBackoffMs[] = {2000, 5000, 10000};
constexpr uint8_t kOomBackoffSteps = sizeof(kOomBackoffMs) / sizeof(kOomBackoffMs[0]);

}

ScriptIcon::~ScriptIcon() {
  for (auto& e : entries_) release(e);
}

void ScriptIcon::release(Entry& e) {
  delete e.anim;
  e.anim = nullptr;
  e.occupied = false;
  e.state = State::kMissing;
  e.nextRetryMs = 0;
  e.retryStep = 0;
  e.name.clear();
}

void ScriptIcon::invalidate() {
  for (auto& e : entries_) release(e);
}

void ScriptIcon::load(Entry& e, int64_t nowMs) {
  delete e.anim;
  e.anim = nullptr;
  e.buf.clear(0x000000u);

  // Capped at one resident frame on purpose: several icons can be cached at once, so animated
  // ones stream rather than each holding a pile of decoded frames.
  GifPlayer* gif = new (std::nothrow) GifPlayer();
  const GifPlayer::OpenResult r =
      gif ? gif->open(e.name, false, 1)
          : GifPlayer::OpenResult::kOom;

  if (r == GifPlayer::OpenResult::kGood) {
    e.anim = gif;
    gif->render(e.buf, nowMs);
    e.state = State::kGood;
    e.retryStep = 0;
    return;
  }
  delete gif;

  if (r == GifPlayer::OpenResult::kOom) {
    e.state = State::kOom;
    if (e.retryStep == 0 && log_)
      log_("icon '" + e.name + "': decode failed, out of memory - will retry");
    e.nextRetryMs = nowMs + kOomBackoffMs[e.retryStep];
    if (e.retryStep + 1 < kOomBackoffSteps) ++e.retryStep;
    return;
  }

  // No GIF under that name, so fall back to the JPG icon of the same id.
  e.state = icon::draw(e.buf, e.name, 0, 0) ? State::kGood : State::kMissing;
  e.retryStep = 0;
}

ScriptIcon::Entry* ScriptIcon::acquire(const std::string& name, int64_t nowMs) {
  for (auto& e : entries_) {
    if (e.occupied && e.name == name) {
      e.lastUsed = ++tick_;
      return &e;
    }
  }

  Entry* victim = &entries_[0];
  for (auto& e : entries_) {
    if (!e.occupied) {
      victim = &e;
      break;
    }
    if (e.lastUsed < victim->lastUsed) victim = &e;
  }

  release(*victim);
  victim->name = name;
  victim->occupied = true;
  victim->lastUsed = ++tick_;
  load(*victim, nowMs);
  return victim;
}

bool ScriptIcon::draw(Canvas& canvas, const std::string& name, int x, int y, int64_t nowMs) {
  if (!nameIsSafe(name) || name.size() > kMaxNameLen) return false;

  Entry* e = acquire(name, nowMs);
  if (e->state == State::kOom && nowMs >= e->nextRetryMs) load(*e, nowMs);
  if (e->state != State::kGood) return false;

  if (e->anim) e->anim->render(e->buf, nowMs);

  for (int row = 0; row < 8; ++row)
    for (int col = 0; col < 8; ++col)
      canvas.setPixel(x + col, y + row, e->buf.getPixel(col, row));
  return true;
}

}
