#include "platform/host/app/macos/AppDelegate.h"

#include <csignal>
#include <memory>
#include <string>

#include "platform/host/app/StatusMenu.h"
#include "platform/host/app/macos/AppPaths.h"
#include "platform/host/app/macos/DefaultsChoice.h"
#include "platform/host/app/macos/HostDriver.h"
#include "platform/host/app/macos/StartAtLogin.h"
#include "platform/host/app/macos/StatusBar.h"
#include "platform/host/app/macos/SystemLoginItem.h"
#include "platform/host/app/macos/Text.h"
#include "platform/linux/hostd/Contract.h"

using namespace awtrix::hostapp;

@implementation HostAppDelegate {
  std::unique_ptr<HostDriver> _driver;
  std::unique_ptr<LoginItem> _loginItem;
  std::unique_ptr<DefaultsChoice> _choice;
  std::unique_ptr<StartAtLogin> _login;
  HostStatusBar* _bar;
  id<NSObject> _activity;
  dispatch_source_t _terminate;
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
  _activity = [NSProcessInfo.processInfo
      beginActivityWithOptions:NSActivityUserInitiatedAllowingIdleSystemSleep | NSActivityLatencyCritical
                        reason:@"Runs the scripts of your clocks"];
  std::signal(SIGTERM, SIG_IGN);
  _terminate = dispatch_source_create(DISPATCH_SOURCE_TYPE_SIGNAL, SIGTERM, 0, dispatch_get_main_queue());
  dispatch_source_set_event_handler(_terminate, ^{
    [NSApp terminate:nil];
  });
  dispatch_resume(_terminate);

  NSBundle* bundle = NSBundle.mainBundle;
  const AppPaths paths = appPaths(utf8(bundle.bundlePath), utf8(NSHomeDirectory()));
  _loginItem = systemLoginItem(paths);
  _choice = std::make_unique<DefaultsChoice>(NSUserDefaults.standardUserDefaults, @"StartAtLogin");
  _login = std::make_unique<StartAtLogin>(*_loginItem, *_choice);
  __weak HostAppDelegate* weakSelf = self;
  _driver = std::make_unique<HostDriver>(hostArguments(paths), dispatch_get_main_queue(), [weakSelf] {
    [weakSelf hostChanged];
  });
  _bar = [[HostStatusBar alloc] initWithDriver:_driver.get()
                                         login:_login.get()
                                       version:utf8([bundle objectForInfoDictionaryKey:@"CFBundleShortVersionString"])
                                       logPath:paths.log];
  _login->launched();
  _driver->start();
}

- (void)hostChanged {
  [_bar refresh];
}

- (BOOL)applicationShouldHandleReopen:(NSApplication*)sender hasVisibleWindows:(BOOL)flag {
  [NSWorkspace.sharedWorkspace openURL:[NSURL URLWithString:nsText(statusPageUrl(awtrix::hostd::kDefaultPort))]];
  return NO;
}

- (void)applicationWillTerminate:(NSNotification*)notification {
  if (_driver) _driver->quit();
  if (_activity) [NSProcessInfo.processInfo endActivity:_activity];
}

@end
