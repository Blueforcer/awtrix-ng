#include "../../support.h"
#include "../../../test/Visuals.h"
#include "platform/tc002/runtime/Tc002QuickSettings.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "media/AwtrixFontAdapter.h"
#include "platform/tc002/voice/KnobGesture.h"

namespace {
using awtrix::Canvas;
using awtrix::Settings;
using awtrix::StateEvent;
using awtrix::StateStore;
using awtrix::Tc002QuickSettings;

enum Ink { kAmber, kCyan, kApp };
constexpr uint32_t kAppColor = 0x123456u;

constexpr auto check = awtrix::test::require;

struct Rig {
  StateStore state;
  Tc002QuickSettings quick{state};
  Canvas canvas{52, 16};
  int changes = 0;
  Rig() {
    state.subscribe([this](StateEvent e) { if (e == StateEvent::SettingsChanged) ++changes; });
  }
  bool draw(int64_t now) {
    canvas.clear(kAppColor);
    return quick.draw(canvas, awtrix::awtrixFont(), now);
  }
  void push(int64_t now) {
    quick.push(now);
  }
  static bool matches(Ink ink, uint32_t pixel) {
    if (ink == kApp) return pixel == kAppColor;
    if (ink == kAmber) return awtrix::test::amber(pixel);
    return (pixel & 255) > 2 * (pixel >> 16) && ((pixel >> 8) & 255) > 2 * (pixel >> 16);
  }
  int count(Ink ink) const {
    return awtrix::test::countPixels(canvas, [ink](uint32_t pixel) { return matches(ink, pixel); });
  }
  int peak(Ink ink) const {
    int value = 0;
    for (int y = 0; y < canvas.height(); ++y)
      for (int x = 0; x < canvas.width(); ++x) {
        const auto pixel = canvas.getPixel(x, y);
        if (matches(ink, pixel))
          value = std::max({value, static_cast<int>(pixel >> 16),
                            static_cast<int>((pixel >> 8) & 255), static_cast<int>(pixel & 255)});
      }
    return value;
  }
  bool shows(Ink ink) const { return count(ink) > 0; }
};

int percentOf(int brightness) { return (brightness * 100 + 127) / 255; }
}

int main() {
  {
    Rig rig;
    rig.push(0);
    check(rig.draw(100) && rig.shows(kAmber) && rig.changes == 0, "a short click opens quick settings and changes nothing");
    Rig turned;
    turned.quick.turn(1, 200);
    check(turned.draw(200), "rotation opens quick settings too");
  }
  {
    Rig rig;
    check(!rig.draw(0) && rig.shows(kApp), "closed screen leaves the app frame alone");
    rig.state.settings().brightness = 120;
    rig.quick.turn(1, 1000);
    check(rig.state.settings().brightness == 120 && rig.changes == 0, "the first detent only shows the screen");
    check(rig.draw(1000), "the screen is up from the first detent");
    rig.quick.turn(1, 1050);
    check(percentOf(rig.state.settings().brightness) == 50 && rig.changes == 1,
          "the second detent snaps 47 % up to 50 %");
    for (int i = 0; i < 20; ++i) rig.quick.turn(1, 1100 + i * 10);
    check(rig.state.settings().brightness == 255 && rig.changes == 11, "brightness stops at 100 % without a change");
    for (int i = 0; i < 40; ++i) rig.quick.turn(-1, 1400 + i * 10);
    check(percentOf(rig.state.settings().brightness) == 5 && rig.changes == 30, "brightness stops at 5 %, never dark");
    for (int step = 10; step <= 100; step += 5) {
      rig.quick.turn(1, 2000 + step);
      check(percentOf(rig.state.settings().brightness) == step, "every 5 % step survives the 0..255 round trip");
    }
  }
  {
    Rig rig;
    rig.state.settings().brightness = 153;
    rig.state.settings().volume = 40;
    rig.quick.turn(1, 0);
    check(rig.draw(0) && !rig.shows(kApp) && rig.shows(kAmber), "the screen shows at once");
    check(rig.draw(700), "open while turned");
    check(!rig.shows(kApp), "the screen covers the app");
    check(rig.peak(kAmber) > rig.peak(kCyan), "brightness is highlighted over the volume row");
    {
      Rig brighter;
      brighter.state.settings().brightness = 204;
      brighter.quick.turn(1, 0);
      brighter.draw(700);
      check(brighter.count(kAmber) > rig.count(kAmber), "a higher brightness lights more of its bar");
    }
    Canvas first = rig.canvas;
    rig.draw(700);
    check(std::memcmp(first.data(), rig.canvas.data(), 52 * 16 * sizeof(uint32_t)) == 0,
          "a frame depends only on the time");

    rig.push(1100);
    rig.quick.turn(1, 1200);
    check(rig.state.settings().volume == 45 && percentOf(rig.state.settings().brightness) == 60,
          "a press moves the knob to the volume");
    const Settings before = rig.state.settings();
    for (int i = 0; i < 12; ++i) rig.quick.turn(-1, 1300 + i * 10);
    check(rig.state.settings().volume == 0, "volume goes down to silence");
    check(rig.state.settings().radioVolume == before.radioVolume &&
              rig.state.settings().appVolume == before.appVolume &&
              rig.state.settings().alertVolume == before.alertVolume,
          "the knob sets the master volume alone");
    check(rig.draw(1550) && rig.peak(kCyan) > rig.peak(kAmber), "the volume row is the selected one");
    rig.push(1600);
    rig.quick.turn(1, 1700);
    check(percentOf(rig.state.settings().brightness) == 65, "the next press loops back to brightness");

    check(rig.draw(1700 + Tc002QuickSettings::kTimeoutMs - 1), "open until the timeout after the last input");
    check(rig.draw(1700 + Tc002QuickSettings::kTimeoutMs + 100) && rig.shows(kAmber),
          "then the screen rains off the panel");
    check(!rig.draw(1700 + Tc002QuickSettings::kTimeoutMs + 450) && rig.shows(kApp), "and hands the panel back");
  }
  {
    Rig rig;
    awtrix::tc002::voice::KnobGesture gesture;
    rig.quick.turn(1, 0);
    gesture.down(Tc002QuickSettings::kTimeoutMs - 100);
    check(!rig.quick.open(Tc002QuickSettings::kTimeoutMs), "press edges do not extend the timeout");
    if (gesture.up(Tc002QuickSettings::kTimeoutMs + 100) ==
        awtrix::tc002::voice::KnobGesture::Action::Click)
      rig.push(Tc002QuickSettings::kTimeoutMs + 100);
    rig.quick.turn(1, Tc002QuickSettings::kTimeoutMs + 150);
    check(rig.quick.open(Tc002QuickSettings::kTimeoutMs + 160) &&
              rig.state.settings().volume == 60,
          "a classified click after timeout reopens the last changed row");
  }
  {
    Rig rig;
    awtrix::tc002::voice::KnobGesture gesture;
    rig.quick.turn(1, 0);
    gesture.down(100);
    check(gesture.tick(600, false) == awtrix::tc002::voice::KnobGesture::Action::None,
          "holding without voice has no action");
    check(gesture.up(700) == awtrix::tc002::voice::KnobGesture::Action::None,
          "releasing a hold does not send a push");
    check(rig.quick.open(700), "holding does not dismiss quick settings");
    check(!rig.quick.open(Tc002QuickSettings::kTimeoutMs), "holding does not postpone the timeout");
  }
  {
    Rig rig;
    rig.state.settings().brightness = 128;
    rig.state.runtime().moodlightMode = true;
    rig.state.runtime().moodlightBrightness = 51;
    int moodlight = 0;
    rig.state.subscribe([&](StateEvent e) { if (e == StateEvent::MoodlightChanged) ++moodlight; });
    rig.quick.turn(1, 0);
    rig.quick.turn(1, 10);
    check(percentOf(rig.state.runtime().moodlightBrightness) == 25 && rig.state.settings().brightness == 128 &&
              moodlight == 1 && rig.changes == 0,
          "under a moodlight the knob sets the moodlight's brightness");
  }
  {
    Rig rig;
    rig.state.settings().volume = 95;
    rig.quick.turn(1, 0);
    rig.push(100);
    for (int i = 0; i < 3; ++i) rig.quick.turn(1, 600 + i * 10);
    check(rig.state.settings().volume == 100, "volume goes up to 100 %");
  }
  {
    Rig rig;
    rig.state.settings().volume = 50;
    rig.state.settings().brightness = 128;
    rig.quick.turn(1, 0);
    check(rig.changes == 0 && rig.draw(100) && rig.peak(kAmber) > rig.peak(kCyan),
          "rotation shows the screen, on brightness");
    rig.quick.turn(1, 600);
    check(percentOf(rig.state.settings().brightness) == 55 && rig.state.settings().volume == 50,
          "the next detent changes brightness");
  }
  {
    Rig rig;
    rig.quick.turn(1, 0);
    rig.quick.dismiss();
    check(!rig.draw(110), "voice immediately takes the panel from quick settings");
    check(!rig.draw(1200), "dismissed quick settings stay closed");
    rig.quick.turn(1, 1300);
    check(rig.draw(1300), "rotation opens it again after voice");
  }
  {
    Rig rig;
    rig.state.runtime().matrixOff = true;
    rig.quick.turn(1, 0);
    rig.push(10);
    check(rig.changes == 0 && !rig.draw(20), "the knob does nothing while the display is off");
    rig.state.runtime().matrixOff = false;
    rig.quick.turn(1, 100);
    rig.state.runtime().matrixOff = true;
    check(!rig.draw(200), "switching the display off closes the screen");
  }
  std::puts("TC002 quick settings contracts passed: steps, limits, selection, master volume, moodlight, timeout, hold, power");
}
