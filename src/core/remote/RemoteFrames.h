#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "core/remote/RemoteWire.h"
#include "core/render/Canvas.h"

namespace awtrix {
namespace remote {

// The pictures the script host sends for the app on the display and the one sliding in. Only those
// two get buffers; a frame is kept once all of its rows are in, so a lost datagram never shows as a
// torn picture. A frame that arrives in one datagram goes straight into its app's buffers, one that
// is split is collected in a shared buffer first.
//
// Wi-Fi delivers frames late and in bursts, so they are played out a little later than the host
// drew them, each at its own time: the delay covers how late 95 of 100 frames arrived over the
// last few seconds. The first frame of an app shows at once.
class RemoteFrames {
 public:
  static constexpr int kSlots = 2;
  static constexpr std::size_t kQueueBytes = 6144;
  static constexpr int kMinQueue = 2;
  static constexpr int kMaxQueue = 8;
  static constexpr int64_t kMaxDelayMs = 150;
  static constexpr int64_t kWindowMs = 5000;
  static constexpr int64_t kResyncMs = 2000;

  void begin(int width, int height);
  // Keeps the buffers of these apps and frees every other one. Empty names are ignored.
  void want(const std::string& current, const std::string& incoming);
  void receive(const wire::Frame& frame, int64_t nowMs);
  // Once per frame of the clock: moves each app to the newest frame that is due.
  void play(int64_t nowMs);

  // False while no frame of this app is in; the canvas is then left alone.
  bool draw(const std::string& app, Canvas& canvas) const;
  int64_t delayMs() const { return delayMs_; }

 private:
  static constexpr int kBuckets = 12;
  static constexpr int64_t kBucketMs = 16;

  struct Entry {
    int64_t hostMs = 0;
    bool used = false;
    std::unique_ptr<uint8_t[]> rgb;
  };
  struct Slot {
    std::string app;
    Entry entries[kMaxQueue];
    int shown = -1;
  };
  struct Window {
    int64_t minOffsetMs = 0;
    uint16_t late[kBuckets] = {};
    int count = 0;
  };

  Slot* slotOf(std::string_view app);
  const Slot* slotOf(std::string_view app) const;
  Entry* place(Slot& slot, int64_t hostMs);
  void keep(Slot& slot, Entry& entry);
  int64_t unwrap(uint16_t stampMs) const;
  void arrived(int64_t hostMs, int64_t nowMs);
  void resync(int64_t offsetMs, int64_t nowMs);
  void resetBuilding();

  int width_ = 0;
  int height_ = 0;
  std::size_t frameBytes_ = 0;
  int queue_ = kMinQueue;
  Slot slots_[kSlots];
  std::unique_ptr<uint8_t[]> building_;
  std::string buildingApp_;
  uint16_t buildingStamp_ = 0;
  uint32_t rowsIn_ = 0;

  bool synced_ = false;
  int64_t lastHostMs_ = 0;
  int64_t lastArrivalMs_ = 0;
  int64_t baseMs_ = 0;
  int64_t delayMs_ = 0;
  int64_t windowStartMs_ = 0;
  Window current_;
  Window previous_;
};

}
}
