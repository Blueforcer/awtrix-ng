#import <AppKit/AppKit.h>

#include <unistd.h>

#include <cstring>

#include "platform/host/app/StatusMenu.h"
#include "platform/host/app/macos/AppDelegate.h"
#include "platform/host/app/macos/AppPaths.h"
#include "platform/host/app/macos/SystemLoginItem.h"
#include "platform/host/app/macos/Text.h"
#include "platform/linux/hostd/Contract.h"

using namespace awtrix::hostapp;

namespace {

bool otherInstance() {
  NSString* identifier = NSBundle.mainBundle.bundleIdentifier;
  if (!identifier) return false;
  for (NSRunningApplication* app in [NSRunningApplication runningApplicationsWithBundleIdentifier:identifier])
    if (app.processIdentifier != getpid() && !app.terminated) return true;
  return false;
}

int removeLoginItem() {
  const AppPaths paths = appPaths(utf8(NSBundle.mainBundle.bundlePath), utf8(NSHomeDirectory()));
  const std::unique_ptr<LoginItem> item = systemLoginItem(paths);
  return !item->enabled() || item->enable(false) ? 0 : 1;
}

}

int main(int argc, char** argv) {
  @autoreleasepool {
    if (argc == 2 && std::strcmp(argv[1], "--remove-login-item") == 0) return removeLoginItem();
    if (otherInstance()) {
      [NSWorkspace.sharedWorkspace openURL:[NSURL URLWithString:nsText(statusPageUrl(awtrix::hostd::kDefaultPort))]];
      return 0;
    }
    NSApplication* app = NSApplication.sharedApplication;
    NS_VALID_UNTIL_END_OF_SCOPE HostAppDelegate* delegate = [[HostAppDelegate alloc] init];
    app.delegate = delegate;
    [app run];
  }
  return 0;
}
