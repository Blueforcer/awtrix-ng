#include "../support.h"

#import <AppKit/AppKit.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "platform/host/app/StatusMenu.h"
#include "platform/host/app/TrayIcon.h"
#include "platform/host/app/macos/AppPaths.h"
#include "platform/host/app/macos/MenuFill.h"
#include "platform/host/app/macos/StatusIcon.h"
#include "platform/host/app/macos/Text.h"

using namespace awtrix::hostapp;
using awtrix::test::check;
using awtrix::test::require;

@interface MenuTarget : NSObject
- (void)choose:(id)sender;
@end

@implementation MenuTarget
- (void)choose:(id)sender {
}
@end

namespace {

NSBitmapImageRep* repOf(NSImage* image, NSInteger pixels) {
  for (NSImageRep* rep in image.representations)
    if (rep.pixelsWide == pixels && [rep isKindOfClass:NSBitmapImageRep.class]) return (NSBitmapImageRep*)rep;
  return nil;
}

bool alphaMatches(NSBitmapImageRep* rep, int pixels) {
  const std::vector<uint8_t> alpha = iconAlpha(pixels);
  if (!rep || rep.pixelsHigh != pixels || alpha.empty()) return false;
  const unsigned char* data = rep.bitmapData;
  const NSInteger stride = rep.bytesPerRow;
  for (int y = 0; y < pixels; ++y)
    for (int x = 0; x < pixels; ++x) {
      const unsigned char* pixel = data + y * stride + x * 4;
      if (pixel[3] != alpha[static_cast<std::size_t>(y) * pixels + x] || pixel[0] || pixel[1] || pixel[2]) return false;
    }
  return true;
}

void icon() {
  NSImage* image = statusIcon(16);
  require(image != nil, "a status icon");
  check([image isTemplate], "it is a template, so the menu bar tints it for light and dark");
  check(image.size.width == 16 && image.size.height == 16, "16 points square");
  check(image.representations.count == 2, "with one picture for normal and one for Retina screens");
  check(alphaMatches(repOf(image, 16), 16), "the normal picture is the shared pixel clock");
  check(alphaMatches(repOf(image, 32), 32), "and so is the Retina picture, at twice the pixels");
  check(repOf(image, 32).size.width == 16, "drawn at the same size in points");
  NSBitmapImageRep* small = repOf(image, 16);
  check(small && [small colorAtX:0 y:0].alphaComponent == 0 && [small colorAtX:3 y:0].alphaComponent == 1,
        "dark corners, lit frame");
  check(statusIcon(0) == nil && statusIcon(2000) == nil, "no icon for sizes the pattern cannot fill");
}

std::vector<MenuItem> menuFor(HostState state, std::string_view status, bool login) {
  MenuInput input;
  input.version = "1.3.0";
  input.host.state = state;
  input.status = status;
  input.logPath = "/Users/x/Library/Logs/AWTRIX Script Host/awtrix-host.log";
  input.startAtLogin = login;
  return buildStatusMenu(input);
}

bool mirrors(NSMenu* menu, const std::vector<MenuItem>& items, id target, SEL action) {
  if (static_cast<std::size_t>(menu.numberOfItems) != items.size()) return false;
  for (std::size_t i = 0; i < items.size(); ++i) {
    NSMenuItem* entry = [menu itemAtIndex:static_cast<NSInteger>(i)];
    const MenuItem& item = items[i];
    if (entry.separatorItem != item.separator) return false;
    if (item.separator) continue;
    const bool clickable = item.enabled && item.action != MenuAction::None;
    if (![entry.title isEqualToString:nsText(item.text)] || entry.enabled != item.enabled ||
        entry.tag != static_cast<NSInteger>(i) || (entry.target == target) != clickable ||
        (entry.action == action) != clickable ||
        entry.state != (item.checkable && item.checked ? NSControlStateValueOn : NSControlStateValueOff))
      return false;
  }
  return true;
}

void menu() {
  MenuTarget* target = [[MenuTarget alloc] init];
  const SEL action = @selector(choose:);
  NSMenu* menu = [[NSMenu alloc] initWithTitle:@"test"];
  menu.autoenablesItems = YES;

  const std::string status =
      R"({"code":"123456","clocks":[{"name":"Desk · left","clock":"http://192.168.1.9","running":true}]})";
  const std::vector<MenuItem> running = menuFor(HostState::Running, status, true);
  fillMenu(menu, running, target, action);
  check(!menu.autoenablesItems, "the menu keeps the enabled state of each line");
  check(mirrors(menu, running, target, action), "the menu shows every line of the model, in order");
  bool code = false, clock = false;
  for (NSMenuItem* entry in menu.itemArray) {
    code = code || [entry.title isEqualToString:@"Pairing code 123456"];
    clock = clock || [entry.title isEqualToString:@"Desk · left · 192.168.1.9 · running"];
  }
  check(code && clock, "with the code and the clock in readable text");

  NSMenuItem* first = [menu itemAtIndex:0];
  const std::vector<MenuItem> loginOff = menuFor(HostState::Running, status, false);
  fillMenu(menu, loginOff, target, action);
  check(mirrors(menu, loginOff, target, action) && [menu itemAtIndex:0] == first,
        "a change of the same shape updates the lines in place, so an open menu does not jump");

  const std::vector<MenuItem> stopped = menuFor(HostState::Stopped, "", true);
  fillMenu(menu, stopped, target, action);
  check(mirrors(menu, stopped, target, action), "a stopped host gets the shorter menu");

  std::vector<MenuItem> odd = stopped;
  odd[0].text = std::string("bad \xff byte");
  fillMenu(menu, odd, target, action);
  check([menu itemAtIndex:0].title.length == 0, "a line that is not UTF-8 shows empty instead of failing");
}

void paths() {
  const AppPaths paths = appPaths("/Applications/AWTRIX Script Host.app", "/Users/Jo Doe");
  const std::vector<std::string> expected = {
      "/Applications/AWTRIX Script Host.app/Contents/MacOS/awtrix-host",
      "--state",
      "/Users/Jo Doe/Library/Application Support/AWTRIX Script Host",
      "--log",
      "/Users/Jo Doe/Library/Logs/AWTRIX Script Host/awtrix-host.log",
      "--exit-with-stdin",
      "--binary",
      "/Applications/AWTRIX Script Host.app/Contents/MacOS/awtrix-script-runner",
      "--webui",
      "/Applications/AWTRIX Script Host.app/Contents/Resources/index.html"};
  check(hostArguments(paths) == expected, "the host gets its data, log, runner and web UI from the user and the bundle");
  check(paths.launchAgents == "/Users/Jo Doe/Library/LaunchAgents" && paths.bundle == "/Applications/AWTRIX Script Host.app",
        "the login agent folder is the user's");
  check(utf8(nsText("Jo · Doe")) == "Jo · Doe" && utf8(nil).empty(), "texts cross between C++ and Cocoa unchanged");
}

}

int main() {
  @autoreleasepool {
    icon();
    menu();
    paths();
  }
  return awtrix::test::finish("macOS menu bar");
}
