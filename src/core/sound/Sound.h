#pragma once

#include <cstdint>

namespace awtrix {
namespace sound {

// Who a playback belongs to. It follows from who asked, never from a field of the request: a
// script's own sound is App, a request over the API, a notification or the device itself is Alert,
// a station is Radio.
enum class Group : uint8_t { Alert, App, Radio };

// The level heard per group in percent, the master already applied. Sinks map it onto their own
// scale: dB curve, PWM duty, DFPlayer steps, linear gain.
struct Volumes {
  uint8_t alert = 0;
  uint8_t app = 0;
  uint8_t radio = 0;

  uint8_t of(Group group) const {
    return group == Group::Alert ? alert : group == Group::App ? app : radio;
  }
  bool operator==(const Volumes& o) const {
    return alert == o.alert && app == o.app && radio == o.radio;
  }
  bool operator!=(const Volumes& o) const { return !(*this == o); }
};

inline uint8_t heard(int master, int group) {
  master = master < 0 ? 0 : master > 100 ? 100 : master;
  group = group < 0 ? 0 : group > 100 ? 100 : group;
  return static_cast<uint8_t>((master * group + 50) / 100);
}

inline Volumes volumesFor(int master, int radio, int app, int alert) {
  Volumes v;
  v.alert = heard(master, alert);
  v.app = heard(master, app);
  v.radio = heard(master, radio);
  return v;
}

}
}
