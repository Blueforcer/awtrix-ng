#pragma once

#include <string>
#include <vector>

#include "core/script/ScriptExtension.h"
#include "platform/linux/script/TcpClients.h"

namespace awtrix::linux_script {

// `import tcp` for scripts: line connections whose events reach the script that opened them on
// the script tick. A connection whose script is not running is closed.
class TcpScripting : public script::ScriptExtension {
 public:
  static constexpr int kPerTick = 32;

  std::vector<std::string> modules() const override { return {"tcp"}; }
  void install(script::ScriptExtensionHost& host) override;
  void tick(script::ScriptExtensionHost& host, const RenderCtx* ctx) override;
  void forget(script::ScriptExtensionHost& host, const std::string& app) override;

 private:
  static int connect(bvm* vm);
  static int send(bvm* vm);
  static int close(bvm* vm);

  TcpClients clients_;
};

}
