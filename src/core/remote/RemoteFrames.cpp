#include "core/remote/RemoteFrames.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>
#include <utility>

#include "core/FrameClock.h"
#include "core/net/RgbRows.h"

namespace awtrix {
namespace remote {

namespace {
constexpr int kMaxHeight = 32;
constexpr int64_t kJumpMs = 1000;
constexpr int64_t kLongAgo = std::numeric_limits<int64_t>::min();

uint32_t allRows(int height) {
  return height >= kMaxHeight ? 0xFFFFFFFFu : ((1u << height) - 1u);
}
}

void RemoteFrames::begin(int width, int height) {
  width_ = width;
  height_ = height > kMaxHeight ? kMaxHeight : height;
  frameBytes_ = static_cast<std::size_t>(width_) * height_ * 3u;
  const std::size_t fits = frameBytes_ ? kQueueBytes / frameBytes_ : 0;
  queue_ = static_cast<int>(std::clamp<std::size_t>(fits, kMinQueue, kMaxQueue));
  for (Slot& slot : slots_) slot = Slot{};
  building_.reset();
  resetBuilding();
  synced_ = false;
  delayMs_ = 0;
}

void RemoteFrames::resetBuilding() {
  buildingApp_.clear();
  buildingStamp_ = 0;
  rowsIn_ = 0;
}

RemoteFrames::Slot* RemoteFrames::slotOf(std::string_view app) {
  if (app.empty()) return nullptr;
  for (Slot& slot : slots_)
    if (slot.app == app) return &slot;
  return nullptr;
}

const RemoteFrames::Slot* RemoteFrames::slotOf(std::string_view app) const {
  if (app.empty()) return nullptr;
  for (const Slot& slot : slots_)
    if (slot.app == app) return &slot;
  return nullptr;
}

void RemoteFrames::want(const std::string& current, const std::string& incoming) {
  for (Slot& slot : slots_)
    if (slot.app.empty() || (slot.app != current && slot.app != incoming)) slot = Slot{};
  for (const std::string* name : {&current, &incoming}) {
    if (name->empty() || slotOf(*name)) continue;
    for (Slot& slot : slots_) {
      if (!slot.app.empty()) continue;
      slot.app = *name;
      break;
    }
  }
  if (!buildingApp_.empty() && !slotOf(buildingApp_)) resetBuilding();
  if (!slotOf(current) && !slotOf(incoming)) building_.reset();
}

int64_t RemoteFrames::unwrap(uint16_t stampMs) const {
  if (!synced_) return stampMs;
  return lastHostMs_ + static_cast<int16_t>(static_cast<uint16_t>(stampMs - static_cast<uint16_t>(lastHostMs_)));
}

void RemoteFrames::resync(int64_t offsetMs, int64_t nowMs) {
  synced_ = true;
  baseMs_ = offsetMs;
  windowStartMs_ = nowMs;
  current_ = Window{};
  current_.minOffsetMs = offsetMs;
  previous_ = current_;
  for (Slot& slot : slots_) {
    for (int i = 0; i < queue_; ++i) {
      Entry& entry = slot.entries[i];
      if (i == slot.shown) entry.hostMs = kLongAgo;
      else entry.used = false;
    }
  }
}

void RemoteFrames::arrived(int64_t hostMs, int64_t nowMs) {
  const int64_t offset = nowMs - hostMs;
  if (!synced_ || nowMs - lastArrivalMs_ > kResyncMs || offset - baseMs_ > kJumpMs || baseMs_ - offset > kJumpMs) {
    resync(offset, nowMs);
    lastHostMs_ = hostMs;
  }
  lastArrivalMs_ = nowMs;
  lastHostMs_ = std::max(lastHostMs_, hostMs);
  if (nowMs - windowStartMs_ >= kWindowMs) {
    previous_ = current_;
    current_ = Window{};
    current_.minOffsetMs = offset;
    windowStartMs_ = nowMs;
  }
  current_.minOffsetMs = std::min(current_.minOffsetMs, offset);
  baseMs_ = previous_.count ? std::min(previous_.minOffsetMs, current_.minOffsetMs) : current_.minOffsetMs;
  const int64_t late = std::max<int64_t>(0, offset - baseMs_);
  ++current_.late[std::min<int64_t>(late / kBucketMs, kBuckets - 1)];
  ++current_.count;
  const int total = current_.count + previous_.count;
  const int wanted = (total * 95 + 99) / 100;
  int seen = 0;
  int bucket = 0;
  for (; bucket < kBuckets - 1; ++bucket) {
    seen += current_.late[bucket] + previous_.late[bucket];
    if (seen >= wanted) break;
  }
  const int64_t ceiling = std::min<int64_t>(kMaxDelayMs, static_cast<int64_t>(queue_ - 1) * kFramePeriodMs);
  delayMs_ = std::min<int64_t>((bucket + 1) * kBucketMs, ceiling);
}

RemoteFrames::Entry* RemoteFrames::place(Slot& slot, int64_t hostMs) {
  if (slot.shown >= 0 && hostMs <= slot.entries[slot.shown].hostMs) return nullptr;
  Entry* oldest = nullptr;
  for (int i = 0; i < queue_; ++i) {
    Entry& entry = slot.entries[i];
    if (entry.used && entry.hostMs == hostMs) return &entry;
  }
  for (int i = 0; i < queue_; ++i) {
    Entry& entry = slot.entries[i];
    if (!entry.used) {
      entry.hostMs = hostMs;
      return &entry;
    }
    if (i != slot.shown && (!oldest || entry.hostMs < oldest->hostMs)) oldest = &entry;
  }
  if (!oldest || oldest->hostMs > hostMs) return nullptr;
  oldest->used = false;
  oldest->hostMs = hostMs;
  return oldest;
}

void RemoteFrames::keep(Slot& slot, Entry& entry) {
  entry.used = true;
  if (slot.shown < 0) slot.shown = static_cast<int>(&entry - slot.entries);
}

void RemoteFrames::receive(const wire::Frame& frame, int64_t nowMs) {
  if (frame.width != width_ || frame.height != height_ || frameBytes_ == 0) return;
  Slot* slot = slotOf(frame.app);
  if (!slot) return;
  if (frame.firstRow == 0 && frame.rows == height_) {
    const int64_t hostMs = unwrap(frame.stampMs);
    arrived(hostMs, nowMs);
    Entry* entry = place(*slot, hostMs);
    if (!entry) return;
    if (!entry->rgb) entry->rgb.reset(new (std::nothrow) uint8_t[frameBytes_]);
    if (!entry->rgb) return;
    std::memcpy(entry->rgb.get(), frame.rgb, frameBytes_);
    keep(*slot, *entry);
    return;
  }
  const std::size_t rowBytes = static_cast<std::size_t>(width_) * 3u;
  if (buildingApp_ != slot->app || buildingStamp_ != frame.stampMs) {
    resetBuilding();
    buildingApp_ = slot->app;
    buildingStamp_ = frame.stampMs;
  }
  if (!building_) building_.reset(new (std::nothrow) uint8_t[frameBytes_]);
  if (!building_) return;
  std::memcpy(building_.get() + frame.firstRow * rowBytes, frame.rgb, frame.rows * rowBytes);
  for (int row = frame.firstRow; row < frame.firstRow + frame.rows; ++row) rowsIn_ |= 1u << row;
  if (rowsIn_ != allRows(height_)) return;
  resetBuilding();
  const int64_t hostMs = unwrap(frame.stampMs);
  arrived(hostMs, nowMs);
  Entry* entry = place(*slot, hostMs);
  if (!entry) return;
  entry->rgb.swap(building_);
  keep(*slot, *entry);
}

void RemoteFrames::play(int64_t nowMs) {
  if (!synced_) return;
  const int64_t due = nowMs - baseMs_ - delayMs_;
  for (Slot& slot : slots_) {
    if (slot.shown < 0) continue;
    int next = slot.shown;
    for (int i = 0; i < queue_; ++i) {
      const Entry& entry = slot.entries[i];
      if (entry.used && entry.hostMs <= due && entry.hostMs > slot.entries[next].hostMs) next = i;
    }
    slot.shown = next;
    const int64_t shownMs = slot.entries[next].hostMs;
    for (int i = 0; i < queue_; ++i) {
      Entry& entry = slot.entries[i];
      if (entry.used && entry.hostMs < shownMs) entry.used = false;
    }
  }
}

bool RemoteFrames::draw(const std::string& app, Canvas& canvas) const {
  const Slot* slot = slotOf(app);
  if (!slot || slot->shown < 0 || canvas.width() != width_ || canvas.height() != height_) return false;
  net::rgb::draw(slot->entries[slot->shown].rgb.get(), canvas);
  return true;
}

}
}
