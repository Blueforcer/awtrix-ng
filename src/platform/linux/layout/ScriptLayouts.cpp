#include "platform/linux/layout/ScriptLayouts.h"

#include <limits>

#include "core/api/JsonReader.h"
#include "platform/linux/layout/LayoutJson.h"
#include "core/memory/CheckedShared.h"
#include "core/render/FontCatalog.h"

namespace awtrix::script {
namespace {

std::string describe(const DispatchDetail& detail) {
  if (detail.message == "out of memory") return "out of memory";
  return detail.field.empty() ? detail.message : detail.field + ": " + detail.message;
}

bool parse(std::string_view json, layout::LayoutSpec& spec, std::string& error) {
  if (json.size() > 16 * 1024 || !api::isWellFormed(json)) {
    error = "layout must be JSON of at most 16384 bytes";
    return false;
  }
  DispatchDetail detail;
  if (!layout::parse(api::JsonReader(json), spec, detail)) {
    error = describe(detail);
    return false;
  }
  return true;
}

}

ScriptLayouts::Entry* ScriptLayouts::find(const std::string& owner, int32_t id) {
  for (auto& entry : entries_)
    if (entry.layout && entry.id == id && entry.owner == owner) return &entry;
  return nullptr;
}

int32_t ScriptLayouts::prepare(const std::string& owner, std::string_view json,
                               std::string& error) {
  if (owner.empty() || display_.height <= 8) {
    error = "no active script or display";
    return 0;
  }
  Entry* slot = nullptr;
  std::size_t owned = 0;
  for (auto& entry : entries_) {
    if (entry.layout && entry.owner == owner) ++owned;
    if (!entry.layout && !slot) slot = &entry;
  }
  if (!slot || owned >= kMaxPerScript || nextId_ == std::numeric_limits<int32_t>::max()) {
    error = "too many layout handles";
    return 0;
  }
  layout::LayoutSpec spec;
  if (!parse(json, spec, error)) return 0;
  DispatchDetail detail;
  auto prepared = layout::PreparedLayout::prepare(std::move(spec),
      display_, resources_, budget_, detail);
  if (!prepared) {
    error = describe(detail);
    return 0;
  }
  if (!slot->owner.assign(owner)) { error = "out of memory"; return 0; }
  slot->id = nextId_++;
  slot->layout = std::move(prepared);
  return slot->id;
}

bool ScriptLayouts::update(const std::string& owner, int32_t handle, std::string_view json,
                           std::string& error) {
  Entry* entry = find(owner, handle);
  if (!entry) {
    error = "unknown layout handle";
    return false;
  }
  layout::LayoutSpec spec;
  if (!parse(json, spec, error)) return false;
  DispatchDetail detail;
  if (!entry->layout->update(std::move(spec), detail)) {
    error = describe(detail);
    return false;
  }
  entry->drawn = false;
  entry->wantsMoreTime = false;
  return true;
}

bool ScriptLayouts::draw(const std::string& owner, int32_t handle, Canvas& canvas,
                         const layout::FrameContext& frame, bool& finished, std::string& error) {
  Entry* entry = find(owner, handle);
  if (!entry) {
    error = "unknown layout handle";
    return false;
  }
  const auto result = entry->layout->draw(canvas, frame);
  entry->drawn = true;
  entry->wantsMoreTime = result.wantsMoreTime;
  finished = result.passesDone;
  return true;
}

bool ScriptLayouts::release(const std::string& owner, int32_t handle) {
  Entry* entry = find(owner, handle);
  if (!entry) return false;
  *entry = Entry{};
  return true;
}

void ScriptLayouts::purge(const std::string& owner) {
  for (auto& entry : entries_)
    if (entry.owner == owner) entry = Entry{};
}

void ScriptLayouts::beginFrame(const std::string& owner) {
  for (auto& entry : entries_)
    if (entry.owner == owner) entry.drawn = false;
}

void ScriptLayouts::restart(const std::string& owner) {
  for (auto& entry : entries_) {
    if (!entry.layout || entry.owner != owner) continue;
    entry.layout->restart();
    entry.drawn = false;
    entry.wantsMoreTime = false;
  }
}

void ScriptLayouts::invalidateAssets() {
  for (auto& entry : entries_)
    if (entry.layout) entry.layout->invalidateAssets();
}

bool ScriptLayouts::wantsMoreTime(const std::string& owner) const {
  for (const auto& entry : entries_)
    if (entry.owner == owner && entry.drawn && entry.wantsMoreTime) return true;
  return false;
}

std::size_t ScriptLayouts::size() const {
  std::size_t count = 0;
  for (const auto& entry : entries_) if (entry.layout) ++count;
  return count;
}

}
