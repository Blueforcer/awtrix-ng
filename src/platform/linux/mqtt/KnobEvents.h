#pragma once

#include "core/mqtt/ButtonEdges.h"
#include "core/net/LinkStatus.h"

namespace awtrix::ha {
class KnobEvents {
 public:
  void observe(bool pressed) { edges_.observe({pressed}); }

  template <typename Publish>
  void tick(const net::LinkStatus& link, bool pressed, uint32_t turns, Publish publish) {
    if (link.phase != net::LinkPhase::Connected) return;
    constexpr auto buttonTopic = "state/buttons/knob";
    if (link.connects != connects_) {
      connects_ = link.connects;
      turns_ = turns;
      publish(buttonTopic, "", true);
      edges_.resync({pressed});
    }
    ControlEdges<1>::Edge edge;
    while (edges_.pop(edge)) publish(buttonTopic, edge.down ? "1" : "0", false);
    if (turns != turns_) {
      const auto turn = static_cast<int32_t>(turns - turns_);
      turns_ = turns;
      publish("event/knob", "{\"turn\":" + std::to_string(turn) + "}", false);
    }
  }
 private:
  ControlEdges<1> edges_;
  uint32_t connects_ = 0;
  uint32_t turns_ = 0;
};
}
