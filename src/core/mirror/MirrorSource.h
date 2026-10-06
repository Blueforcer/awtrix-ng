#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "core/mirror/MirrorFilter.h"
#include "core/net/Datagram.h"
#include "core/render/Canvas.h"
#include "core/render/PageInfo.h"

namespace awtrix {
namespace mirror {

// The sharing side: keeps the viewers that subscribed and sends each of them the composed frame
// whenever the filter admits the page, or Idle when it does not. Unchanged content is repeated once
// a second, which both proves the clock is still there and repairs a lost datagram. A viewer with
// another panel size is only told the size of this one and never takes a place.
class Source {
 public:
  static constexpr std::size_t kMaxViewers = 8;
  static constexpr int64_t kLeaseMs = 6000;
  static constexpr int64_t kKeepaliveMs = 1000;

  Source(net::IDatagramSink& sink, Filter filter, int width, int height)
      : sink_(sink), filter_(std::move(filter)), width_(static_cast<uint16_t>(width)),
        height_(static_cast<uint16_t>(height)) {}

  void setFilter(Filter filter);
  // A known viewer renews its lease. A new one is refused while all places are taken.
  bool subscribe(const net::Endpoint& viewer, int width, int height, int64_t nowMs);
  void leave(const net::Endpoint& viewer);
  void expire(int64_t nowMs);
  void publish(const Canvas& frame, const PageInfo* page, int64_t nowMs);
  // Tells every viewer there is nothing to see any more, then forgets them.
  void stop();
  // Forgets every viewer without a word, for when the network is gone.
  void drop() {
    count_ = 0;
    force_ = true;
  }
  std::size_t viewers() const { return count_; }

 private:
  struct Viewer {
    net::Endpoint endpoint;
    int64_t expiresMs = 0;
  };

  bool ensureBuffer();
  void sendAll(std::size_t length);
  void sendFrame(const Canvas& frame, const PageInfo& page);
  void sendIdle();
  void sendIdleTo(const net::Endpoint& viewer);

  net::IDatagramSink& sink_;
  Filter filter_;
  std::array<Viewer, kMaxViewers> viewers_{};
  std::size_t count_ = 0;
  std::unique_ptr<uint8_t[]> buffer_;
  bool shared_ = false;
  bool force_ = true;
  uint32_t fingerprint_ = 0;
  uint16_t frameNumber_ = 0;
  uint16_t width_;
  uint16_t height_;
  int64_t lastSentMs_ = 0;
};

}
}
