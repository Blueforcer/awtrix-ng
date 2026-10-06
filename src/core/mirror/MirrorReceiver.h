#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "core/mirror/MirrorFilter.h"
#include "core/mirror/MirrorStatus.h"
#include "core/mirror/MirrorWire.h"
#include "core/net/Datagram.h"
#include "core/render/Canvas.h"

namespace awtrix {
namespace mirror {

// The viewing side for one shared clock: subscribes to it, assembles the rows of each frame and
// hands over a frame only once all of its rows are in, so a lost datagram never shows as a torn
// picture. The frame buffers are allocated with the first frame that is to be shown.
class Receiver {
 public:
  static constexpr int64_t kSubscribeEveryMs = 2000;
  static constexpr int64_t kSilenceMs = 3000;

  Receiver(net::IDatagramSink& sink, const net::Endpoint& source, Filter filter, int width,
           int height);

  const net::Endpoint& source() const { return source_; }
  void setFilter(Filter filter);
  void receive(const wire::Packet& packet, int64_t nowMs);
  void tick(int64_t nowMs);
  // Tells the shared clock to stop sending.
  void leave();

  FollowState state() const { return state_; }
  bool showing() const { return state_ == FollowState::Showing; }
  uint16_t sourceWidth() const { return sourceWidth_; }
  uint16_t sourceHeight() const { return sourceHeight_; }
  void draw(Canvas& canvas) const;

 private:
  bool ensureBuffers();
  void accept(const wire::Packet& packet);
  void subscribe();

  net::IDatagramSink& sink_;
  net::Endpoint source_;
  Filter filter_;
  int width_;
  int height_;
  std::size_t frameBytes_;
  std::unique_ptr<uint8_t[]> shown_;
  std::unique_ptr<uint8_t[]> building_;
  std::unique_ptr<bool[]> rowIn_;
  bool assembling_ = false;
  uint16_t buildingFrame_ = 0;
  int rowsIn_ = 0;
  FollowState state_ = FollowState::Waiting;
  uint16_t sourceWidth_ = 0;
  uint16_t sourceHeight_ = 0;
  int64_t lastHeardMs_ = 0;
  int64_t lastSubscribeMs_ = 0;
  bool heard_ = false;
  bool subscribed_ = false;
};

}
}
