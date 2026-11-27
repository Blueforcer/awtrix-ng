#include "platform/host/app/macos/StatusBar.h"

#include <cstddef>
#include <vector>

#include "platform/host/app/StatusFetch.h"
#include "platform/host/app/StatusMenu.h"
#include "platform/host/app/macos/MenuFill.h"
#include "platform/host/app/macos/StatusIcon.h"
#include "platform/host/app/macos/Text.h"
#include "platform/linux/hostd/Contract.h"

using namespace awtrix::hostapp;
namespace hostd = awtrix::hostd;

namespace {

constexpr int kIconPoints = 16;

}

@implementation HostStatusBar {
  HostDriver* _driver;
  StartAtLogin* _login;
  std::string _version;
  std::string _logPath;
  NSStatusItem* _item;
  NSMenu* _menu;
  std::vector<MenuItem> _items;
  std::string _status;
  BOOL _fetching;
}

- (instancetype)initWithDriver:(HostDriver*)driver
                         login:(StartAtLogin*)login
                       version:(const std::string&)version
                       logPath:(const std::string&)logPath {
  if (!(self = [super init])) return nil;
  _driver = driver;
  _login = login;
  _version = version;
  _logPath = logPath;
  _menu = [[NSMenu alloc] initWithTitle:@"AWTRIX Script Host"];
  _menu.delegate = self;
  _item = [NSStatusBar.systemStatusBar statusItemWithLength:NSSquareStatusItemLength];
  _item.button.image = statusIcon(kIconPoints);
  _item.menu = _menu;
  [self refresh];
  return self;
}

- (void)refresh {
  const HostView view = _driver->view();
  if (!view.up()) _status.clear();
  _item.button.toolTip = nsText(statusLine(view, hostd::kDefaultPort));
  _item.button.appearsDisabled = !view.up();
  [self rebuild];
}

- (void)rebuild {
  MenuInput input;
  input.version = _version;
  input.host = _driver->view();
  input.status = _status;
  input.logPath = _logPath;
  input.startAtLogin = _login->on();
  _items = buildStatusMenu(input);
  fillMenu(_menu, _items, self, @selector(choose:));
}

- (void)menuNeedsUpdate:(NSMenu*)menu {
  [self rebuild];
  [self fetch];
}

- (void)fetch {
  if (_fetching || !_driver->view().up()) return;
  _fetching = YES;
  __weak HostStatusBar* weakSelf = self;
  dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
    std::string body;
    if (!fetchStatus(hostd::kDefaultPort, body)) body.clear();
    dispatch_async(dispatch_get_main_queue(), ^{
      [weakSelf fetched:body];
    });
  });
}

- (void)fetched:(const std::string&)body {
  _fetching = NO;
  _status = _driver->view().up() ? body : std::string();
  [self rebuild];
}

- (void)choose:(NSMenuItem*)sender {
  const NSInteger index = sender.tag;
  if (index < 0 || static_cast<std::size_t>(index) >= _items.size()) return;
  const MenuItem item = _items[static_cast<std::size_t>(index)];
  switch (item.action) {
    case MenuAction::None:
      break;
    case MenuAction::Start:
      _driver->start();
      break;
    case MenuAction::CopyCode:
      [NSPasteboard.generalPasteboard clearContents];
      [NSPasteboard.generalPasteboard setString:nsText(item.value) forType:NSPasteboardTypeString];
      break;
    case MenuAction::OpenClock:
    case MenuAction::OpenStatusPage:
      [self openAddress:item.value];
      break;
    case MenuAction::OpenLog:
      [self openLog:item.value];
      break;
    case MenuAction::StartAtLogin:
      [self toggleLogin];
      break;
    case MenuAction::Quit:
      [NSApp terminate:nil];
      break;
  }
}

- (void)openAddress:(const std::string&)address {
  NSURL* url = [NSURL URLWithString:nsText(address)];
  if (url) [NSWorkspace.sharedWorkspace openURL:url];
}

- (void)openLog:(const std::string&)path {
  NSURL* file = [NSURL fileURLWithPath:nsText(path)];
  if ([NSWorkspace.sharedWorkspace openURL:file]) return;
  NSURL* folder = file.URLByDeletingLastPathComponent;
  [NSFileManager.defaultManager createDirectoryAtURL:folder withIntermediateDirectories:YES attributes:nil error:nil];
  [NSWorkspace.sharedWorkspace openURL:folder];
}

- (void)toggleLogin {
  if (_login->toggle()) {
    [self rebuild];
    return;
  }
  NSAlert* alert = [[NSAlert alloc] init];
  alert.messageText = @"Start at login did not change";
  alert.informativeText = @"macOS did not allow it. Check the login items in System Settings.";
  [NSApp activateIgnoringOtherApps:YES];
  [alert runModal];
}

@end
