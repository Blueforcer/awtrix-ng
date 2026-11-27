#pragma once

#import <Foundation/Foundation.h>

#include <optional>
#include <string>

#include "platform/host/app/macos/LoginItem.h"

namespace awtrix::hostapp {

class DefaultsChoice : public LoginChoice {
 public:
  DefaultsChoice(NSUserDefaults* defaults, NSString* key) : defaults_(defaults), key_(key) {}

  std::optional<bool> stored() const override;
  void store(bool on) override;

 private:
  NSUserDefaults* defaults_;
  NSString* key_;
};

}
