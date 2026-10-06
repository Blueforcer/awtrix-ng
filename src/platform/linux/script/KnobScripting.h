#pragma once

#include <cstdint>
#include <functional>
#include "core/script/ScriptExtension.h"

namespace awtrix::script {
class KnobScripting final : public ScriptExtension {
 public:
  KnobScripting(std::function<int64_t()> clock, std::function<void()> restart);
  std::vector<std::string> modules() const override { return {}; }
  std::vector<std::string> hooks() const override { return {"on_knob"}; }
  void install(ScriptExtensionHost& host) override { host_ = &host; }
  void forget(ScriptExtensionHost&, const std::string& app) override;
  void hidden(ScriptExtensionHost& host, const std::string& app) override { forget(host, app); }

  bool turn(const std::string& app, int direction);
  bool press(const std::string& app);
  void held(const std::string& app);
  bool release(const std::string& app);

 private:
  void restart();
  ScriptExtensionHost* host_ = nullptr;
  std::function<int64_t()> clock_;
  std::function<void()> restart_;
  std::string owner_;
  int64_t pressedAt_ = 0;
  bool down_ = false;
  bool longSent_ = false;
};
}
