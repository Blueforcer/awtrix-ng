#include "../../support.h"
#include "platform/tc002/runtime/Tc002Input.h"
#include "platform/tc002/contract/InputIdentity.h"
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <linux/input.h>
#include <utility>
#include <vector>

namespace {
constexpr auto check = awtrix::test::require;
}

int main() {
  check(awtrix::tc002::inputKind(-1) == awtrix::tc002::InputKind::Other, "invalid descriptor has no input identity");
  const int other = open("/dev/null", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
  check(other >= 0 && awtrix::tc002::inputKind(other) == awtrix::tc002::InputKind::Other,
        "an unrelated character device is not a knob or button device");
  close(other);
  using Knob = awtrix::Tc002InputDecoder::Knob;
  awtrix::Tc002InputDecoder decoder;
  std::vector<std::pair<int, bool>> edges;
  std::vector<Knob> knob;
  const awtrix::Tc002InputDecoder::Sink sink{[&](int index, bool state) { edges.emplace_back(index, state); },
                                             [&](Knob event) { knob.push_back(event); }};
  for (const auto key : {KEY_DOWN, KEY_LEFT, KEY_RIGHT}) {
    check(decoder.event(false, EV_KEY, key, 1, sink), "press");
    check(decoder.event(false, EV_KEY, key, 2, sink), "repeat ignored");
    check(decoder.event(false, EV_KEY, key, 0, sink), "release");
  }
  check(edges == std::vector<std::pair<int, bool>>{{0,true},{0,false},{1,true},{1,false},{2,true},{2,false}},
        "top buttons preserve ordered short presses");
  check(knob.empty(), "top buttons never reach the knob");
  edges.clear();
  check(decoder.event(false, EV_KEY, KEY_UP, 1, sink), "knob push");
  check(decoder.event(false, EV_KEY, KEY_UP, 2, sink), "knob repeat ignored");
  check(decoder.event(false, EV_KEY, KEY_UP, 0, sink), "knob release");
  check(edges.empty() && knob == (std::vector<Knob>{Knob::Down, Knob::Up}), "knob push is its own pair of edges");
  knob.clear();
  decoder.event(false, EV_KEY, KEY_LEFT, 1, sink);
  decoder.event(false, EV_KEY, KEY_UP, 1, sink);
  decoder.event(false, EV_KEY, KEY_UP, 0, sink);
  check(edges == std::vector<std::pair<int, bool>>{{1,true}} && knob == (std::vector<Knob>{Knob::Down, Knob::Up}),
        "knob push leaves a held select alone");
  decoder.event(false, EV_KEY, KEY_LEFT, 0, sink);
  check(edges.back() == std::make_pair(1, false), "select releases on its own button");
  edges.clear();
  knob.clear();
  for (int value : {8,1,13,11}) {
    check(decoder.event(true, EV_ABS, ABS_X, value, sink), "rotary phase");
    check(decoder.event(true, EV_SYN, SYN_REPORT, 0, sink), "rotary sync");
  }
  check(knob == std::vector<Knob>{Knob::Clockwise, Knob::Counterclockwise} && edges.empty(),
        "detents turn the knob, not the buttons");
  check(decoder.clockwise() == 1 && decoder.counterclockwise() == 1, "detent counters");
  knob.clear();
  for (int value : {1,11,8,11,13,1,0,8,8,1}) decoder.event(true, EV_ABS, ABS_X, value, sink);
  check(knob == std::vector<Knob>{Knob::Clockwise}, "incomplete/crossed phases do not turn");
  knob.clear();
  const awtrix::Tc002InputDecoder::Sink buttonsOnly{sink.button, nullptr};
  decoder.event(true, EV_ABS, ABS_X, 8, buttonsOnly);
  check(decoder.event(true, EV_ABS, ABS_X, 1, buttonsOnly), "detent without a knob handler");
  check(decoder.event(false, EV_KEY, KEY_UP, 1, buttonsOnly) && decoder.event(false, EV_KEY, KEY_UP, 0, buttonsOnly),
        "push without a knob handler");
  check(knob.empty() && decoder.clockwise() == 3, "a missing knob handler drops knob events but keeps counting");
  check(!decoder.event(false, EV_SYN, SYN_DROPPED, 0, sink), "dropped records fail closed");
  check(!decoder.event(false, EV_KEY, KEY_POWER, 1, sink), "power/reset never mapped");
  check(!decoder.event(false, EV_KEY, KEY_LEFT, 3, sink), "invalid key value rejected");
  check(!decoder.event(true, EV_REL, REL_X, 1, sink), "unexpected rotary format rejected");
  std::puts("TC002 input contracts passed: ordered edges, knob detents and push, optional knob, overflow");
}
