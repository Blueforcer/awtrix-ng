#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "core/script/ScriptExtension.h"
#include "platform/linux/ble/BleBackend.h"
#include "platform/linux/ble/BleTypes.h"

namespace awtrix::ble {

// `import ble` for scripts. The module keeps each script's callbacks by request id; the backend's
// events wait here until the script host's tick hands them to the script that asked, in order.
class BleScripting : public script::ScriptExtension {
 public:
  static constexpr std::size_t kQueue = 64;

  explicit BleScripting(BleBackend& backend) : backend_(backend) {}

  std::vector<std::string> modules() const override { return {"ble"}; }
  void install(script::ScriptExtensionHost& host) override;
  void tick(script::ScriptExtensionHost& host, const RenderCtx* ctx) override;
  void forget(script::ScriptExtensionHost& host, const std::string& app) override;

  // Any thread: an answer or event the backend has for a script.
  void push(BleEvent event);

 private:
  struct Pending {
    bool done = false;
    bool dropped = false;
  };
  static int native(bvm* vm);
  std::string call(const std::string& app, const std::string& op, const std::string& args, uint32_t id);
  bool pop(BleEvent& event, bool& deliver);

  BleBackend& backend_;
  std::mutex mutex_;
  std::map<std::pair<std::string, uint32_t>, Pending> pending_;
  std::deque<BleEvent> events_;
};

}
