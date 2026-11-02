#pragma once

#include <string>
#include <vector>

#include "core/script/ScriptExtension.h"
#include "platform/linux/secrets/SecretService.h"

namespace awtrix::secrets {

class SecretScripting : public script::ScriptExtension {
 public:
  explicit SecretScripting(Service& service) : service_(service) {}
  std::vector<std::string> modules() const override { return {"secret"}; }
  void install(script::ScriptExtensionHost& host) override;

 private:
  static int has(bvm* vm);
  static int allows(bvm* vm);

  Service& service_;
};

}
