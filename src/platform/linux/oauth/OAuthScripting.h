#pragma once

#include <string>
#include <vector>

#include "core/script/ScriptExtension.h"
#include "platform/linux/oauth/OAuthService.h"

namespace awtrix::oauth {

// `import oauth`: requests made with the app's sign-in. Tokens never enter the script VM.
class OAuthScripting : public script::ScriptExtension {
 public:
  static constexpr int kPerTick = 16;

  explicit OAuthScripting(Service& service) : service_(service) {}
  std::vector<std::string> modules() const override { return {"oauth"}; }
  void install(script::ScriptExtensionHost& host) override;
  void tick(script::ScriptExtensionHost& host, const RenderCtx* ctx) override;
  void forget(script::ScriptExtensionHost& host, const std::string& app) override;

 private:
  static int request(bvm* vm);
  static int ready(bvm* vm);

  Service& service_;
};

}
