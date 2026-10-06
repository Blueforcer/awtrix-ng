#pragma once

#include <cstddef>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include "core/memory/CheckedStorage.h"
#include "core/render/PageContent.h"
#include "core/render/PageIcon.h"

#include "core/Command.h"
#include "core/effects/EffectRegistry.h"
#include "core/payload/AppSpec.h"
#include "core/payload/ScrollSpec.h"
#include "core/render/Canvas.h"
#include "core/render/DisplayProfile.h"
#include "core/render/Font.h"
#include "core/render/FontCatalog.h"

namespace awtrix::layout {

using checked::CheckedString;
using checked::CheckedArray;
namespace storage = checked::storage;

struct Box { int x = 0, y = 0, width = 0, height = 0; };
enum class Kind : uint8_t { Text, Image, Chart, Progress, Draw };

// A palette carried by value, so a layout never shares or allocates palette storage. A named
// palette keeps its name until the layout is prepared.
struct Paint {
  bool set = false;
  CheckedString name;
  render::Palette palette{};
  bool blend = true;
  uint16_t spanPx = 0;
  float speed = 0.0f;
};

struct Region {
  CheckedString id;
  Box box;
  Kind kind = Kind::Text;
  CheckedString text;
  CheckedString font = "small";
  CheckedString asset;
  Align align = Align::Center;
  Align valign = Align::Center;
  uint32_t color = 0xFFFFFF;
  bool hasColor = false;
  bool usesPalette = false;
  Paint palette;
  uint32_t trackColor = 0x202020;
  // Fragments of text; a fragment without a colour uses the region's.
  CheckedArray<text::TextRun> fragments;
  TextCase textCase = TextCase::Inherit;
  int blinkMs = 0, fadeMs = 0;
  render::DrawProgram draw;
  ScrollSpec scroll;
  int repeat = -1; // inherit the page's repeat; zero never holds the page
  CheckedArray<int> values;
  bool bars = false;
  bool autoscale = true;
  int minimum = 0;
  int maximum = 100;
  int progress = 0;
  // Filled by the compiler's metadata probe, never trusted from a wire representation.
  int mediaWidth = 0, mediaHeight = 0;
  std::size_t mediaBytes = 0;
  bool valid() const {
    return id.valid() && text.valid() && font.valid() && asset.valid() && values.valid() &&
        fragments.valid() && draw.valid() && palette.name.valid();
  }
};

struct LayoutSpec {
  unsigned version = 1;
  CheckedArray<Region> regions;
  uint32_t background = 0;
  bool hasBackground = false;
  Paint palette;
  CheckedString effect;
  float effectSpeed = 1.0f;
  bool hasEffectSpeed = false;
  CheckedString overlay;
  bool valid() const {
    return regions.valid() && effect.valid() && overlay.valid() && palette.name.valid();
  }
};

struct Limits {
  std::size_t regions = 16;
  std::size_t scrollers = 8;
  std::size_t assets = 4;
  std::size_t chartPoints = 128;
  std::size_t textBytes = 8192;
  // Shared by every admitted page and script handle, including both transition pages.
  std::size_t preparedBytes = 64 * 1024;
};

class Budget {
 public:
  explicit Budget(const Limits& limits = {}) : limits_(limits) {}
  const Limits& limits() const { return limits_; }
  std::size_t used() const { return used_.load(); }
  bool acquire(std::size_t bytes);
  void release(std::size_t bytes);
 private:
  Limits limits_;
  std::atomic<std::size_t> used_{0};
};

struct Resources {
  const FontCatalog* fonts = nullptr;
  const IPageIcon* icons = nullptr;
  const EffectRegistry* effects = nullptr;
  const EffectRegistry* overlays = nullptr;
};

using FrameContext = PageFrameContext;
using FrameResult = PageFrameResult;

// Owns prepared geometry and per-ID animation state. All operations have render-thread affinity:
// HTTP/MQTT adapters marshal admission onto the loop, as do script callbacks. Image decoders
// must not run concurrently. Ordinary frames reuse font metrics; an asset invalidation can
// reprepare the layout on the loop before drawing. Drawing never parses JSON.
class PreparedLayout {
 public:
  static std::unique_ptr<PreparedLayout> prepare(LayoutSpec spec,
      DisplayProfile display, const Resources& resources, std::shared_ptr<Budget> budget,
      DispatchDetail& error);
  ~PreparedLayout();
  bool update(LayoutSpec spec, DispatchDetail& error);
  FrameResult draw(Canvas& canvas, const FrameContext& frame);

  void restart();
  void invalidateAssets();
  void inheritState(PreparedLayout& previous);
  uint64_t revision() const { return revision_; }
  bool hasOverlay() const { return overlay_ != nullptr; }
  bool repeats() const { return repeats_; }

 private:
  struct Item;
  PreparedLayout(DisplayProfile display, const Resources& resources, std::shared_ptr<Budget> budget);
  bool compile(LayoutSpec spec, DispatchDetail& error, PreparedLayout* previous = nullptr);
  Item* reusableImage(const Region& region) const;
  DisplayProfile display_;
  Resources resources_;
  std::shared_ptr<Budget> budget_;
  LayoutSpec spec_;
  std::unique_ptr<Item[]> items_;
  std::size_t itemCount_ = 0;
  std::size_t charge_ = 0;
  uint64_t revision_ = 0;
  bool assetsDirty_ = false;
  bool repeats_ = false;
  IEffect* effect_ = nullptr;
  IEffect* overlay_ = nullptr;
};

// Validates regions, shared by JSON, Berry and prepared-handle updates.
bool resolve(const LayoutSpec& spec, DisplayProfile display, const Resources& resources,
             const Limits& limits, CheckedArray<Region>& regions, DispatchDetail& error);

}
