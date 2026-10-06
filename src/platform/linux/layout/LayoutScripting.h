#pragma once

#include "core/script/ScriptExtension.h"
#include "platform/linux/layout/ScriptLayouts.h"

namespace awtrix::layout {

class LayoutScripting final : public script::ScriptExtension {
 public:
  LayoutScripting(DisplayProfile display, const Resources& resources, std::shared_ptr<Budget> budget)
      : layouts_(display, resources, std::move(budget)) {}
  std::vector<std::string> modules() const override { return {"layout"}; }
  void install(script::ScriptExtensionHost& host) override;
  void beginFrame(script::ScriptExtensionHost&, const std::string& app) override { layouts_.beginFrame(app); }
  void forget(script::ScriptExtensionHost&, const std::string& app) override { layouts_.purge(app); }
  void hidden(script::ScriptExtensionHost&, const std::string& app) override { layouts_.restart(app); }
  bool holds(const std::string& app) const override { return layouts_.wantsMoreTime(app); }
  void invalidateAssets() { layouts_.invalidateAssets(); }

 private:
  script::ScriptLayouts layouts_;
};

}
