#include "../../support.h"
#include <cstdio>
#include <cstdlib>

#include "platform/tc002/voice/KnobGesture.h"
using awtrix::tc002::voice::KnobGesture;
using awtrix::test::require;
int main() {
  using Action = KnobGesture::Action;
  KnobGesture gesture;
  gesture.down(100);
  require(gesture.tick(599, true) == Action::None);
  require(gesture.tick(600, true) == Action::Start);
  require(gesture.tick(1200, true) == Action::None);
  require(!gesture.turn());
  require(gesture.up(1300) == Action::None);
  require(gesture.tick(1400, true) == Action::None);
  gesture.down(2000);
  require(gesture.tick(2450, true) == Action::None && gesture.up(2450) == Action::Click);
  gesture.down(3000);
  require(gesture.turn());
  require(gesture.tick(4100, true) == Action::None &&
        gesture.up(4200) == Action::None);
  gesture.down(5000);
  require(gesture.tick(6100, false) == Action::None);
  require(gesture.up(6200) == Action::None);
  gesture.down(7000);
  require(gesture.tick(8000, true) == Action::Start);
  gesture.cancel();
  require(gesture.up(8200) == Action::None);
  std::puts(
      "voice gesture: 500 ms hold, hands-free release, click, rotation "
      "and cancellation passed");
}
