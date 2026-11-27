#include "platform/host/app/macos/SystemLoginItem.h"

#import <ServiceManagement/ServiceManagement.h>

#include "platform/host/app/macos/LaunchAgentLoginItem.h"
#include "platform/host/app/macos/Text.h"

namespace awtrix::hostapp {
namespace {

class API_AVAILABLE(macos(13.0)) ServiceLoginItem : public LoginItem {
 public:
  bool enabled() const override { return SMAppService.mainAppService.status == SMAppServiceStatusEnabled; }

  bool enable(bool on) override {
    SMAppService* service = SMAppService.mainAppService;
    return on ? [service registerAndReturnError:nil] : [service unregisterAndReturnError:nil];
  }
};

}

std::unique_ptr<LoginItem> systemLoginItem(const AppPaths& paths) {
  if (@available(macOS 13.0, *)) return std::make_unique<ServiceLoginItem>();
  return std::make_unique<LaunchAgentLoginItem>(paths.launchAgents, utf8(NSBundle.mainBundle.bundleIdentifier),
                                                paths.bundle);
}

}
