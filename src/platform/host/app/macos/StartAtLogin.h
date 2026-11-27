#pragma once

#include "platform/host/app/macos/LoginItem.h"

namespace awtrix::hostapp {

class StartAtLogin {
 public:
  StartAtLogin(LoginItem& item, LoginChoice& choice) : item_(item), choice_(choice) {}

  bool launched();
  bool on() const;
  bool toggle();

 private:
  bool apply(bool on);

  LoginItem& item_;
  LoginChoice& choice_;
};

}
