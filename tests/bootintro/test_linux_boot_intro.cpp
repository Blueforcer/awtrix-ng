#include "../support.h"
// Tc002BootIntro against a scripted boot sound: when the intro's t = 0 falls, how long it owns the
// panel and what the info screen after it shows.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "platform/tc002/runtime/BootInfoWide.h"
#include "platform/tc002/runtime/BootIntroWide.h"
#include "core/render/BootScreen.h"
#include "media/AwtrixFontAdapter.h"
#include "platform/tc002/runtime/Tc002BootIntro.h"

using namespace awtrix;

namespace {

int& failures = awtrix::test::failures();

using awtrix::test::check;

constexpr char kVersion[] = "9.8.7";

bool same(const Canvas& a, const Canvas& b) { return std::memcmp(a.data(), b.data(), a.size() * sizeof(uint32_t)) == 0; }

Canvas introAt(int64_t startMs, int64_t nowMs) {
  Canvas c(52, 16);
  render::drawBootIntroWide(c, awtrixFont(), startMs, nowMs);
  return c;
}

Canvas infoAt(const std::string& address, int64_t startMs, int64_t nowMs) {
  Canvas c(52, 16);
  render::drawBootInfoWide(c, render::BootInfo{kVersion, address}, startMs, nowMs);
  return c;
}

struct ScriptedSound : BootSoundClock {
  Start start = Start::Pending;
  int64_t audibleAtMs = -1;
  int cancels = 0;
  Start poll(int64_t& at) override {
    at = start == Start::Audible ? audibleAtMs : -1;
    return start;
  }
  void cancel() override { ++cancels; }
};

struct Address {
  std::string value;
  int asked = 0;
  std::string operator()() {
    ++asked;
    return value;
  }
};

void withoutSound() {
  Address address;
  Tc002BootIntro intro(kVersion, [&] { return address(); });
  Canvas c(52, 16);
  bool matches = true;
  for (int64_t now = 1000; now < 1000 + render::kBootIntroWideMs; now += 24) {
    matches &= intro.draw(c, awtrixFont(), now) && same(c, introAt(1000, now));
    address.value = "192.0.2.9";
  }
  address.value.clear();
  check(matches, "without a sound the intro starts with the first frame");
  check(intro.introStartMs() == 1000 && !intro.soundSynchronized(), "t = 0 is the first frame");
  check(address.asked == 0, "the address is only looked up once the intro is over");
  const int64_t end = 1000 + render::kBootIntroWideMs;
  bool version = true;
  for (int64_t now = end; now < end + render::kBootInfoHoldMs; now += 40)
    version &= intro.draw(c, awtrixFont(), now) && same(c, infoAt("", end, now));
  check(version, "without an address the info screen shows the version");
  check(address.asked == 1 && intro.shownAddress().empty(), "an address known too late is not shown");
  check(!intro.draw(c, awtrixFont(), end + render::kBootInfoHoldMs), "then the apps take over");
  check(!intro.draw(c, awtrixFont(), 90000), "and the sequence stays over");
}

void soundStartsTheClock() {
  ScriptedSound sound;
  Tc002BootIntro intro(kVersion, nullptr, &sound);
  Canvas c(52, 16);
  const Canvas first = introAt(0, 0);
  bool held = true;
  for (int64_t now = 500; now < 800; now += 24) held &= intro.draw(c, awtrixFont(), now) && same(c, first);
  check(held && intro.introStartMs() == -1, "the first frame holds while the sound is pending");
  sound.start = BootSoundClock::Start::Audible;
  sound.audibleAtMs = 830;
  check(intro.draw(c, awtrixFont(), 812) && same(c, first), "a sound audible a little later still shows t = 0");
  check(intro.introStartMs() == 830 && intro.soundSynchronized(), "t = 0 is when the sound is audible");
  check(intro.draw(c, awtrixFont(), 2000) && same(c, introAt(830, 2000)), "the intro runs on the sound's clock");
  check(intro.draw(c, awtrixFont(), 830 + render::kBootIntroWideMs - 1), "until its last millisecond");
  check(intro.draw(c, awtrixFont(), 830 + render::kBootIntroWideMs) &&
            same(c, infoAt("", 830 + render::kBootIntroWideMs, 830 + render::kBootIntroWideMs)),
        "then the info screen");
  check(sound.cancels == 0, "a sound that started is left to play");
}

void soundThatNeverStarts() {
  {
    ScriptedSound sound;
    Tc002BootIntro intro(kVersion, nullptr, &sound);
    Canvas c(52, 16);
    intro.draw(c, awtrixFont(), 100);
    intro.draw(c, awtrixFont(), 100 + Tc002BootIntro::kSoundWaitMs - 1);
    check(sound.cancels == 0 && intro.introStartMs() == -1, "the sound gets kSoundWaitMs to start");
    check(intro.draw(c, awtrixFont(), 100 + Tc002BootIntro::kSoundWaitMs), "the intro then runs");
    check(sound.cancels == 1 && intro.introStartMs() == 100 + Tc002BootIntro::kSoundWaitMs && !intro.soundSynchronized(),
          "without the late sound, from that frame on");
    intro.draw(c, awtrixFont(), 3000);
    check(sound.cancels == 1, "cancelled once");
  }
  {
    ScriptedSound sound;
    Tc002BootIntro intro(kVersion, nullptr, &sound);
    Canvas c(52, 16);
    intro.draw(c, awtrixFont(), 100);
    sound.start = BootSoundClock::Start::Failed;
    intro.draw(c, awtrixFont(), 340);
    check(intro.introStartMs() == 340 && sound.cancels == 0, "a failed sound starts the intro at once");
  }
}

void addressAfterTheIntro() {
  Address address;
  address.value = "192.0.2.7:8080";
  Tc002BootIntro intro(kVersion, [&] { return address(); });
  Canvas c(52, 16);
  intro.draw(c, awtrixFont(), 0);
  const int64_t end = render::kBootIntroWideMs;
  check(intro.draw(c, awtrixFont(), end) && address.asked == 1 && intro.shownAddress() == "192.0.2.7:8080",
        "a known address follows the intro");
  address.value = "198.51.100.1";
  bool matches = true;
  int64_t now = end;
  for (; now < end + 30000 && intro.draw(c, awtrixFont(), now); now += 40)
    matches &= same(c, infoAt("192.0.2.7:8080", end, now));
  check(matches, "the info screen shows the version and the address");
  check(address.asked == 1, "with the address latched when the intro ended");
  check(now == end + render::kBootInfoHoldMs, "and hands over after the hold: " + std::to_string(now - end));
  check(!intro.draw(c, awtrixFont(), now + 40), "then the apps take over");
}

}

int main() {
  withoutSound();
  soundStartsTheClock();
  soundThatNeverStarts();
  addressAfterTheIntro();
  if (failures) return 1;
  std::puts("linux boot intro: ok");
  return 0;
}
