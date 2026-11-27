#include "platform/host/app/macos/StartAtLogin.h"

namespace awtrix::hostapp {

bool StartAtLogin::launched() {
  if (choice_.stored()) return true;
  return apply(true);
}

bool StartAtLogin::on() const { return item_.enabled(); }

bool StartAtLogin::toggle() { return apply(!item_.enabled()); }

bool StartAtLogin::apply(bool on) {
  if (item_.enabled() != on && (!item_.enable(on) || item_.enabled() != on)) return false;
  choice_.store(on);
  return true;
}

}
