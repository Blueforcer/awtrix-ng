#include "platform/linux/layout/Layout.h"
#include "platform/linux/layout/LayoutError.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <utility>

#include "core/icons/IconSource.h"
#include "core/render/DrawProgram.h"
#include "core/render/Gfx2d.h"
#include "core/render/PaletteStore.h"
#include "core/render/ScrollResolver.h"
#include "core/render/TextRenderer.h"

namespace awtrix::layout {
namespace {
bool failure(DispatchDetail& error, const DetailPath& field, const char* message) {
  return layoutFailure(error, field, message);
}
bool sameBox(const Box& a, const Box& b) {
  return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}
// A picture from a URL is fitted to its region; any other image keeps its own size, which only
// has to fit the display.
Box imageBounds(const Region& r, DisplayProfile display) {
  if (icons::parse(r.asset.view()).remote()) return {0, 0, r.box.width, r.box.height};
  return {0, 0, display.width, display.height};
}
bool sameScroll(const ScrollSpec& a, const ScrollSpec& b) {
  return a.hasMode == b.hasMode && a.mode == b.mode &&
      a.hasDirection == b.hasDirection && a.direction == b.direction &&
      a.hasEntry == b.hasEntry && a.entry == b.entry &&
      a.hasWhenFits == b.hasWhenFits && a.whenFits == b.whenFits &&
      a.hasSpeed == b.hasSpeed && a.speed == b.speed &&
      a.hasGap == b.hasGap && a.gap == b.gap &&
      a.hasHoldMs == b.hasHoldMs && a.holdMs == b.holdMs;
}
std::size_t contentBytes(const Region& r) {
  // Item already contains the region itself; charge its owned buffers and allocation overhead.
  // Media gets its worst-case native pixel reservation before any decoder runs.
  return 128 + r.id.capacity() + r.text.capacity() + r.font.capacity() +
              r.asset.capacity() + 4 + r.values.capacity() * sizeof(int) +
              r.fragments.capacity() * sizeof(text::TextRun) +
              r.draw.commands.capacity() * sizeof(render::DrawCommand) +
              r.draw.data.capacity() * sizeof(uint32_t) + r.draw.text.capacity() +
              r.palette.name.capacity();
}
// Resolves named palettes at prepare time.
bool resolvePalette(Paint& paint) {
  return paint.name.empty() || render::lookupPalette(std::string(paint.name.view()), paint.palette);
}
// Borrows the palette without shared ownership; the ramp must not outlive the paint.
render::ColorRamp rampOf(const Paint& paint) {
  render::ColorRamp ramp;
  if (!paint.set) return ramp;
  ramp.pal = std::shared_ptr<const render::Palette>(std::shared_ptr<const render::Palette>(),
                                                    &paint.palette);
  ramp.blend = paint.blend;
  ramp.spanPx = paint.spanPx;
  ramp.speed = paint.speed;
  return ramp;
}
}

bool Budget::acquire(std::size_t bytes) {
  std::size_t before = used_.load();
  do {
    if (bytes > limits_.preparedBytes || before > limits_.preparedBytes - bytes) return false;
  } while (!used_.compare_exchange_weak(before, before + bytes));
  return true;
}
void Budget::release(std::size_t bytes) {
  std::size_t before = used_.load();
  while (!used_.compare_exchange_weak(before, bytes <= before ? before - bytes : 0)) {}
}

namespace {
bool resolveInPlace(LayoutSpec& spec, DisplayProfile display, const Resources& resources,
                    const Limits& limits, DispatchDetail& error) {
  if (!display.valid() || display.width > 32767 || display.height > 32767)
    return failure(error, "layout", "invalid display");
  if (spec.version != 1) return failure(error, "layout.version", "must be 1");
  if (spec.regions.empty() || spec.regions.size() > limits.regions)
    return failure(error, "layout.regions", "too many regions");
  if (!spec.effect.empty() && !(resources.effects && resources.effects->find(spec.effect.view())))
    return failure(error, "layout.effect", "unknown name");
  if (!spec.overlay.empty() && !(resources.overlays && resources.overlays->find(spec.overlay.view())))
    return failure(error, "layout.overlay", "unknown name");
  std::size_t scrollers = 0, assets = 0, textBytes = 0;
  for (std::size_t index = 0; index < spec.regions.size(); ++index) {
    Region& r = spec.regions[index];
    if (r.text.size() > limits.textBytes || textBytes > limits.textBytes - r.text.size() ||
        r.id.size() > 64 || r.font.size() > 96 || r.asset.size() > 8192 ||
        r.values.size() > limits.chartPoints)
      return failure(error, "layout.regions", "content too large");
    if (!r.valid()) return memoryFailure(error);
    const DetailPath path = DetailPath("layout.regions.") + r.id.view();
    if (r.id.empty() || r.id.size() > 64) return failure(error, path, "invalid ID");
    for (std::size_t before = 0; before < index; ++before)
      if (spec.regions[before].id == r.id) return failure(error, path, "duplicate ID");
    const auto& b = r.box;
    if (b.x < 0 || b.y < 0 || b.width <= 0 || b.height <= 0 ||
        static_cast<int64_t>(b.x) + b.width > display.width ||
        static_cast<int64_t>(b.y) + b.height > display.height)
      return failure(error, path + ".box", "outside the display");
    if (r.usesPalette && !r.palette.set && !spec.palette.set)
      return failure(error, path + ".color", "needs palette");
    if ((r.kind == Kind::Text || r.kind == Kind::Draw) &&
        !(resources.fonts && resources.fonts->find(r.font)))
      return failure(error, path + ".font", "unknown font");
    if (r.kind == Kind::Text) {
      if ((r.scroll.hasSpeed && (r.scroll.speed < 0 || r.scroll.speed > 1000000)) ||
          (r.scroll.hasGap && (r.scroll.gap < 0 || r.scroll.gap > 32767)) ||
          (r.scroll.hasHoldMs && (r.scroll.holdMs < 0 || r.scroll.holdMs > 1000000)))
        return failure(error, path + ".scroll", "out of range");
      if (r.repeat < -1 || r.repeat > 1000000)
        return failure(error, path + ".repeat", "expected 0..1000000");
      if (!r.scroll.hasMode || r.scroll.mode != ScrollMode::Static) ++scrollers;
      textBytes += r.text.size();
    } else if (r.kind == Kind::Image) {
      if (r.asset.empty() || !resources.icons)
        return failure(error, path + ".icon", "icons unavailable");
      const Box bounds = imageBounds(r, display);
      if (!resources.icons->nativeInfo(r.asset, bounds.width, bounds.height,
                               r.mediaWidth, r.mediaHeight, r.mediaBytes) ||
          r.mediaWidth <= 0 || r.mediaHeight <= 0 || r.mediaWidth > display.width ||
          r.mediaHeight > display.height || r.mediaBytes == 0)
        return failure(error, path + ".icon", "unknown or too large");
      ++assets;
    } else if (r.kind == Kind::Chart) {
      if (r.values.empty() || r.values.size() > limits.chartPoints ||
          (!r.autoscale && r.minimum >= r.maximum))
        return failure(error, path + ".chart", "invalid chart");
    } else if (r.kind == Kind::Progress && (r.progress < 0 || r.progress > 100)) {
      return failure(error, path + ".progress", "expected 0..100");
    }
  }
  if (scrollers > limits.scrollers || assets > limits.assets || textBytes > limits.textBytes)
    return failure(error, "layout", "too much text, scrolling or icons");
  return true;
}
}

bool resolve(const LayoutSpec& spec, DisplayProfile display, const Resources& resources,
             const Limits& limits, CheckedArray<Region>& out, DispatchDetail& error) {
  LayoutSpec next = spec;
  if (!next.valid()) return memoryFailure(error);
  if (!resolveInPlace(next, display, resources, limits, error)) return false;
  out = std::move(next.regions);
  return true;
}

struct PreparedLayout::Item {
  Region region;
  const FontEntry* font = nullptr;
  text::TextMetrics metrics, upperMetrics;
  std::unique_ptr<IPageIcon> image;
  bool imagePending = true;
  bool imageReady = false;
  int64_t startMs = -1;
  int cycles = 0;
  uint64_t scrollSignature = 0;
};

PreparedLayout::PreparedLayout(DisplayProfile display, const Resources& resources,
                               std::shared_ptr<Budget> budget)
    : display_(display), resources_(resources), budget_(std::move(budget)) {}
PreparedLayout::~PreparedLayout() { if (budget_) budget_->release(charge_); }

std::unique_ptr<PreparedLayout> PreparedLayout::prepare(LayoutSpec spec,
    DisplayProfile display, const Resources& resources, std::shared_ptr<Budget> budget,
    DispatchDetail& error) {
  if (!spec.valid()) { memoryFailure(error); return nullptr; }
  if (!budget) { failure(error, "layout", "no memory budget"); return nullptr; }
  std::unique_ptr<PreparedLayout> next(new (std::nothrow) PreparedLayout(display, resources, std::move(budget)));
  if (!next || !next->compile(std::move(spec), error)) {
    if (!next) memoryFailure(error);
    return nullptr;
  }
  next->revision_ = 1;
  return next;
}

bool PreparedLayout::compile(LayoutSpec spec, DispatchDetail& error, PreparedLayout* previous) {
  if (!spec.valid()) return memoryFailure(error);
  const auto& limits = budget_->limits();
  if (spec.regions.empty() || spec.regions.size() > limits.regions)
    return failure(error, "layout.regions", "too many regions");
  // Admit the retained regions, owned buffers and checked shared-owner reservation
  // before compiling the consumed input into items.
  std::size_t metadata = sizeof(PreparedLayout) + 2 * 128;
  std::size_t textBytes = 0;
  for (const auto& r : spec.regions) {
    if (r.id.size() > 64 || r.font.size() > 96 || r.asset.size() > 8192 ||
        r.values.size() > limits.chartPoints || r.text.size() > limits.textBytes ||
        textBytes > limits.textBytes - r.text.size())
      return failure(error, "layout.regions", "content too large");
    textBytes += r.text.size();
    const std::size_t bytes = sizeof(Item) + contentBytes(r);
    if (bytes > limits.preparedBytes || metadata > limits.preparedBytes - bytes)
      return failure(error, "layout", "too large");
    metadata += bytes;
  }
  if (!budget_->acquire(metadata)) return failure(error, "layout", "layout memory full");
  charge_ = metadata;
  if (!resolveInPlace(spec, display_, resources_, limits, error)) return false;
  std::size_t media = 0;
  for (const auto& r : spec.regions) {
    const Item* inherited = previous ? previous->reusableImage(r) : nullptr;
    const std::size_t bytes = r.kind == Kind::Image
        ? r.mediaBytes - (inherited ? inherited->region.mediaBytes : 0) : 0;
    if (bytes > limits.preparedBytes || media > limits.preparedBytes - bytes)
      return failure(error, "layout", "too large");
    media += bytes;
  }
  if (!budget_->acquire(media)) return failure(error, "layout", "layout memory full");
  charge_ += media;
  if (!resolvePalette(spec.palette)) return failure(error, "layout.palette", "unknown palette");
  items_.reset(new (std::nothrow) Item[spec.regions.size()]);
  if (!items_) return memoryFailure(error);
  itemCount_ = spec.regions.size();
  std::size_t index = 0;
  for (auto& input : spec.regions) {
    Item* item = &items_[index++];
    item->region = std::move(input);
    if (!resolvePalette(item->region.palette))
      return failure(error, DetailPath("layout.regions.") + item->region.id.view() + ".palette",
                     "unknown palette");
    const Region& r = item->region;
    if (r.kind == Kind::Text || r.kind == Kind::Draw) item->font = resources_.fonts->find(r.font);
    if (r.kind == Kind::Text) {
      repeats_ |= r.repeat > 0;
      item->metrics = text::measureInk(*item->font->font, r.text);
      item->upperMetrics = text::measureInk(*item->font->font, r.text, r.textCase != TextCase::AsTyped);
    } else if (r.kind == Kind::Image) {
      if (previous && previous->reusableImage(r)) {
        // Borrow only the reservation until every other region has compiled.
        // A failed decoder must leave all of the old page's images untouched.
        item->imagePending = false;
        continue;
      }
      item->image = resources_.icons->create();
      const Box bounds = imageBounds(r, display_);
      if (!item->image || item->image->beginNativeBounded(item->region.asset, bounds.width,
                                                  bounds.height, item->region.mediaBytes) != IconLoad::kGood)
        return failure(error, DetailPath("layout.regions.") + item->region.id.view() + ".icon",
                        "icon could not be decoded");
      if (item->image->width() != item->region.mediaWidth || item->image->height() != item->region.mediaHeight)
        return failure(error, DetailPath("layout.regions.") + item->region.id.view() + ".icon", "icon changed");
      item->imagePending = false;
      item->imageReady = true;
    }
  }
  spec.regions = {};
  spec_ = std::move(spec);
  effect_ = resources_.effects ? resources_.effects->find(spec_.effect.view()) : nullptr;
  overlay_ = resources_.overlays ? resources_.overlays->find(spec_.overlay.view()) : nullptr;
  if (previous) {
    for (std::size_t i = 0; i < itemCount_; ++i) {
      auto& item = items_[i];
      if (item.region.kind != Kind::Image || item.imageReady || item.imagePending) continue;
      auto* old = previous->reusableImage(item.region);
      item.image = std::move(old->image);
      item.imageReady = true;
      old->imageReady = false;
      // One allocation, one reservation: transfer ownership without releasing
      // and reacquiring capacity in the shared budget.
      charge_ += old->region.mediaBytes;
      previous->charge_ -= old->region.mediaBytes;
    }
  }
  return true;
}

PreparedLayout::Item* PreparedLayout::reusableImage(const Region& region) const {
  if (region.kind != Kind::Image) return nullptr;
  for (std::size_t i = 0; i < itemCount_; ++i) {
    auto& old = items_[i];
    const auto& before = old.region;
    if (region.id == before.id && before.kind == Kind::Image && sameBox(region.box, before.box) &&
        region.asset == before.asset && region.mediaBytes >= before.mediaBytes &&
        region.mediaWidth == before.mediaWidth && region.mediaHeight == before.mediaHeight &&
        old.imageReady && !old.imagePending) return &old;
  }
  return nullptr;
}

void PreparedLayout::inheritState(PreparedLayout& previous) {
  for (std::size_t i = 0; i < itemCount_; ++i) {
    Item* next = &items_[i];
    for (std::size_t j = 0; j < previous.itemCount_; ++j) {
      Item* old = &previous.items_[j];
      const Region& a = next->region;
      const Region& b = old->region;
      if (a.id == b.id && a.kind == b.kind && sameBox(a.box, b.box) &&
          a.text == b.text && a.font == b.font && a.repeat == b.repeat &&
          sameScroll(a.scroll, b.scroll)) {
        next->startMs = old->startMs;
        next->cycles = old->cycles;
        next->scrollSignature = old->scrollSignature;
        if (a.kind == Kind::Image && a.asset == b.asset && a.mediaBytes >= b.mediaBytes &&
            a.mediaWidth == b.mediaWidth && a.mediaHeight == b.mediaHeight && old->imageReady) {
          if (!old->imagePending) {
            next->image = std::move(old->image);
            next->imagePending = false;
            next->imageReady = true;
            old->imageReady = false;
          }
        }
        break;
      }
    }
  }
}
bool PreparedLayout::update(LayoutSpec spec, DispatchDetail& error) {
  std::unique_ptr<PreparedLayout> next(new (std::nothrow) PreparedLayout(display_, resources_, budget_));
  if (!next) return memoryFailure(error);
  if (!next->compile(std::move(spec), error, this)) return false;
  next->inheritState(*this);
  std::swap(spec_, next->spec_);
  std::swap(items_, next->items_);
  std::swap(itemCount_, next->itemCount_);
  std::swap(charge_, next->charge_);
  std::swap(repeats_, next->repeats_);
  std::swap(effect_, next->effect_);
  std::swap(overlay_, next->overlay_);
  ++revision_;
  return true;
}
void PreparedLayout::restart() {
  for (std::size_t i = 0; i < itemCount_; ++i) { items_[i].startMs = -1; items_[i].cycles = 0; }
}
void PreparedLayout::invalidateAssets() {
  for (std::size_t i = 0; i < itemCount_; ++i) {
    if (items_[i].region.kind == Kind::Image) {
      items_[i].imagePending = true;
      assetsDirty_ = true;
    }
  }
}

namespace {
struct Motion { double x = 0; int cycles = 0; bool animates = false; int period = 0; };
uint64_t scrollSignature(const Region& r, const ScrollDefaults& d) {
  uint64_t hash = 1469598103934665603ull;
  const int fields[] = {
    static_cast<int>(r.scroll.hasMode ? r.scroll.mode : d.mode),
    static_cast<int>(r.scroll.hasDirection ? r.scroll.direction : d.direction),
    static_cast<int>(r.scroll.hasEntry ? r.scroll.entry : d.entry),
    static_cast<int>(r.scroll.hasWhenFits ? r.scroll.whenFits : d.whenFits),
    r.scroll.hasSpeed ? r.scroll.speed : d.speed,
    r.scroll.hasGap ? r.scroll.gap : d.gap,
    r.scroll.hasHoldMs ? r.scroll.holdMs : d.holdMs,
  };
  for (int value : fields) { hash ^= static_cast<uint32_t>(value); hash *= 1099511628211ull; }
  return hash;
}
ScrollDefaults nativeDefaults(ScrollDefaults defaults) {
  // Clamps the inherited scroll defaults to keep the loop period within range.
  defaults.speed = std::clamp(defaults.speed, 0, 1000000);
  defaults.gap = std::clamp(defaults.gap, 0, 32767);
  defaults.holdMs = std::clamp(defaults.holdMs, 0, 1000000);
  return defaults;
}
Motion position(const Region& r, const text::TextMetrics& metrics, const FrameContext& frame,
                int64_t started) {
  Motion m;
  m.x = aligned(r.align, r.box.x, r.box.width, metrics.inkWidth()) - metrics.inkLeft;
  const auto mode = r.scroll.hasMode ? r.scroll.mode : frame.scrollDefaults.mode;
  const auto whenFits = r.scroll.hasWhenFits ? r.scroll.whenFits : frame.scrollDefaults.whenFits;
  if (!metrics.hasInk() || mode == ScrollMode::Static ||
      (whenFits == ScrollWhenFits::Static && metrics.inkWidth() <= r.box.width)) return m;
  m.animates = true;
  const bool left = (r.scroll.hasDirection ? r.scroll.direction : frame.scrollDefaults.direction) == ScrollDirection::Left;
  const bool off = (r.scroll.hasEntry ? r.scroll.entry : frame.scrollDefaults.entry) == ScrollEntry::Offscreen;
  // Speed in percent: 100 moves 0.5 px per 24 ms of elapsed time.
  const double speed = (r.scroll.hasSpeed ? r.scroll.speed : frame.scrollDefaults.speed) / 4.8;
  const int hold = r.scroll.hasHoldMs ? r.scroll.holdMs : frame.scrollDefaults.holdMs;
  const int gap = r.scroll.hasGap ? r.scroll.gap : frame.scrollDefaults.gap;
  const int repeat = r.repeat >= 0 ? r.repeat : frame.repeat;
  const double elapsed = std::max<int64_t>(0, frame.nowMs - started);
  const double near = r.box.x - metrics.inkLeft;
  const double far = r.box.x + r.box.width - 1 - metrics.inkRight;
  const double start = off ? (left ? r.box.x + r.box.width : r.box.x - metrics.advance) : (left ? near : far);
  const double end = left ? r.box.x - metrics.advance : r.box.x + r.box.width;
  const double sign = left ? -1 : 1;
  if (speed <= 0) { m.x = start; return m; }
  const double initialHold = off ? 0 : hold;
  const double active = std::max(0.0, elapsed - initialHold);
  if (mode == ScrollMode::Loop) {
    const int64_t period = static_cast<int64_t>(metrics.advance) + gap;
    m.period = static_cast<int>(std::clamp<int64_t>(period, 1, INT32_MAX));
    double travelled = active * speed / 1000.0;
    m.cycles = static_cast<int>(std::min(1000000000.0, std::floor(travelled / m.period)));
    if (frame.parkAfterPasses && repeat > 0 && m.cycles >= repeat) {
      m.cycles = repeat; travelled = 0;
    }
    m.x = start + sign * std::fmod(travelled, static_cast<double>(m.period));
  } else if (mode == ScrollMode::Bounce) {
    const double a = left ? near : far, b = left ? far : near;
    const double distance = std::abs(b - a);
    if (distance == 0) { m.animates = false; m.x = a; return m; }
    const double travelMs = distance * 1000.0 / speed;
    const double legMs = travelMs + hold;
    const double cycleMs = legMs * 2;
    const double entryMs = std::abs(b - start) * 1000.0 / speed;
    const double firstCycleMs = entryMs + hold + travelMs;
    double regular = elapsed;
    if (off && elapsed < firstCycleMs) {
      if (elapsed < entryMs) m.x = start + (b - start) * (elapsed / entryMs);
      else {
        const double t = std::clamp((elapsed - entryMs - hold) / travelMs, 0.0, 1.0);
        m.x = b + (a - b) * t;
      }
      return m;
    }
    if (off) regular -= firstCycleMs;
    m.cycles = static_cast<int>(std::min(1000000000.0, std::floor(regular / cycleMs) + (off ? 1 : 0)));
    if (frame.parkAfterPasses && repeat > 0 && m.cycles >= repeat) {
      m.cycles = repeat; m.x = a;
    } else {
      const double phase = std::fmod(regular, cycleMs);
      const bool back = phase >= legMs;
      const double t = std::clamp(((back ? phase - legMs : phase) - hold) / travelMs, 0.0, 1.0);
      m.x = back ? b + (a - b) * t : a + (b - a) * t;
    }
  } else {
    const double travelMs = std::max(1.0, std::abs(end - start) * 1000.0 / speed);
    const double cycleMs = travelMs + initialHold;
    m.cycles = static_cast<int>(std::min(1000000000.0, std::floor(elapsed / cycleMs)));
    if (frame.parkAfterPasses && repeat > 0 && m.cycles >= repeat) {
      m.cycles = repeat; m.x = end;
    } else {
      const double phase = std::fmod(elapsed, cycleMs);
      m.x = start + sign * std::max(0.0, phase - initialHold) * speed / 1000.0;
    }
  }
  return m;
}
void drawChart(Canvas& canvas, const Region& r, const render::ColorSource& paint) {
  int minimum = r.minimum, maximum = r.maximum;
  if (r.autoscale) {
    minimum = 0; maximum = 1;
    for (int value : r.values) { minimum = std::min(minimum, value); maximum = std::max(maximum, value); }
  }
  const double span = static_cast<double>(maximum) - minimum;
  const auto fraction = [&](int value) {
    return std::clamp((static_cast<double>(value) - minimum) / span, 0.0, 1.0);
  };
  const auto level = [&](int value, int height) { return static_cast<int>(fraction(value) * height); };
  const auto color = [&](int value) { return paint.at(static_cast<float>(fraction(value))); };
  const auto& b = r.box;
  const int n = static_cast<int>(r.values.size());
  if (r.bars) {
    const int zero = level(0, b.height);
    for (int i = 0; i < n; ++i) {
      const int x = i * b.width / n, end = (i + 1) * b.width / n;
      const int width = end - x > 2 ? end - x - 1 : end - x;
      const int value = level(r.values[i], b.height);
      canvas.fillRect(b.x + x, b.y + b.height - std::max(zero, value), width,
                      std::abs(value - zero), color(r.values[i]));
    }
  } else if (n == 1) {
    canvas.setPixel(b.x, b.y + b.height - 1 - level(r.values[0], b.height - 1), color(r.values[0]));
  } else {
    for (int i = 0; i + 1 < n; ++i)
      canvas.drawLine(b.x + i * (b.width - 1) / (n - 1),
          b.y + b.height - 1 - level(r.values[i], b.height - 1),
          b.x + (i + 1) * (b.width - 1) / (n - 1),
          b.y + b.height - 1 - level(r.values[i + 1], b.height - 1), color(r.values[i]));
  }
}
}

FrameResult PreparedLayout::draw(Canvas& canvas, const FrameContext& frame) {
  FrameResult result;
  if (assetsDirty_) {
    assetsDirty_ = false;
    DispatchDetail error;
    // Resolve the complete layout again: a replacement's native size must still fit its
    // region. Preparing all assets before swapping keeps the previous valid page on any
    // metadata, decoder or allocation failure.
    LayoutSpec replacement = spec_;
    if (replacement.valid() && replacement.regions.reserve(itemCount_)) {
      bool copied = true;
      for (std::size_t i = 0; i < itemCount_ && copied; ++i)
        copied = replacement.regions.push_back(items_[i].region);
      if (copied) update(std::move(replacement), error);
    }
  }
  result.revision = revision_;
  if (canvas.width() != display_.width || canvas.height() != display_.height) return result;
  EffectSettings settings;
  settings.speed = spec_.effectSpeed;
  settings.hasSpeed = spec_.hasEffectSpeed;
  settings.ramp = rampOf(spec_.palette);
  // Effects are shared instances: they are handed the borrowed palette only while drawing.
  const auto paintWith = [&](IEffect* effect) {
    effect->setSettings(settings);
    effect->render(canvas, effect->animationStep(frame.nowMs));
    effect->setSettings(EffectSettings{});
  };
  if (effect_) paintWith(effect_);
  else canvas.clear(spec_.background);
  const auto originalClip = canvas.clipRect();
  bool participant = false;
  FrameContext safeFrame = frame;
  safeFrame.scrollDefaults = nativeDefaults(frame.scrollDefaults);
  for (std::size_t i = 0; i < itemCount_; ++i) {
    Item* item = &items_[i];
    const Region& r = item->region;
    const auto& b = r.box;
    canvas.restoreClipRect({std::max(originalClip.left, b.x), std::max(originalClip.top, b.y),
        std::min(originalClip.right, b.x + b.width - 1), std::min(originalClip.bottom, b.y + b.height - 1)});
    const uint32_t color = r.hasColor ? r.color : frame.defaultColor;
    render::ColorRamp ramp;
    if (r.usesPalette) ramp = rampOf(r.palette.set ? r.palette : spec_.palette);
    const render::ColorSource fill(color, ramp.valid() ? &ramp : nullptr);
    if (r.kind == Kind::Text) {
      const bool upper =
          r.textCase == TextCase::Upper || (r.textCase == TextCase::Inherit && frame.uppercase);
      const text::TextMetrics& metrics = upper ? item->upperMetrics : item->metrics;
      text::TextPaint paint;
      paint.flat = color;
      paint.upper = upper;
      paint.fadeMs = r.fadeMs;
      paint.blinkMs = r.blinkMs;
      paint.nowMs = frame.nowMs;
      paint.runs = r.fragments.data();
      paint.runCount = r.fragments.size();
      if (fill.ramp) {
        paint.ramp = fill.ramp;
        paint.rampOriginPx = ramp.originAt(frame.nowMs, metrics.advance);
      }
      const uint64_t signature = scrollSignature(r, safeFrame.scrollDefaults);
      if (item->startMs < 0 || signature != item->scrollSignature) {
        item->startMs = frame.nowMs;
        item->scrollSignature = signature;
      }
      const Motion motion = position(r, metrics, safeFrame, item->startMs);
      item->cycles = motion.cycles;
      const int repeat = r.repeat >= 0 ? r.repeat : frame.repeat;
      if (motion.animates && repeat > 0) {
        participant = true;
        result.wantsMoreTime |= motion.cycles < repeat;
      }
      const int baseline = aligned(r.valign, b.y, b.height, metrics.inkHeight()) - metrics.inkTop;
      int x = static_cast<int>(std::floor(motion.x));
      if (motion.period > 0) {
        const int64_t period = motion.period;
        const int64_t left = canvas.clipRect().left;
        const int64_t right = canvas.clipRect().right;
        const int64_t first = static_cast<int64_t>(std::ceil(
            static_cast<double>(left - x - metrics.inkRight) / period));
        const int64_t last = static_cast<int64_t>(std::floor(
            static_cast<double>(right - x - metrics.inkLeft) / period));
        const int64_t count = std::min<int64_t>(std::max<int64_t>(0, last - first + 1), b.width + 2);
        for (int64_t copy = 0; copy < count; ++copy) {
          const int64_t origin = x + (first + copy) * period;
          if (origin >= INT32_MIN && origin <= INT32_MAX)
            text::drawRun(canvas, *item->font->font, static_cast<int>(origin), baseline, r.text, paint);
        }
      } else text::drawRun(canvas, *item->font->font, x, baseline, r.text, paint);
    } else if (r.kind == Kind::Image) {
      if (item->imageReady) {
        item->image->advance(frame.nowMs);
        item->image->blit(canvas, aligned(r.align, b.x, b.width, item->image->width()),
                          aligned(r.valign, b.y, b.height, item->image->height()));
      }
    } else if (r.kind == Kind::Chart) {
      drawChart(canvas, r, fill);
    } else if (r.kind == Kind::Draw) {
      render::drawProgram(canvas, *item->font->font, pageBaseline(*item->font) - 1, r.draw, color,
                          b.x, b.y);
    } else {
      render::fillProgress(canvas, b.x, b.y, b.width, b.height, r.progress, fill, r.trackColor);
    }
  }
  canvas.restoreClipRect(originalClip);
  if (overlay_) paintWith(overlay_);
  result.passesDone = participant && !result.wantsMoreTime;
  return result;
}
}
