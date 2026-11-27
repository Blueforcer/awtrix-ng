#pragma once

#include <string>

#include "platform/host/app/macos/LoginItem.h"

namespace awtrix::hostapp {

std::string launchAgentPlist(const std::string& label, const std::string& app);

class LaunchAgentLoginItem : public LoginItem {
 public:
  LaunchAgentLoginItem(std::string folder, std::string label, std::string app);

  bool enabled() const override;
  bool enable(bool on) override;
  std::string path() const;

 private:
  std::string folder_;
  std::string label_;
  std::string app_;
};

}
