#include "platform/host/app/macos/MenuFill.h"

#include <cstddef>

#include "platform/host/app/macos/Text.h"

namespace awtrix::hostapp {
namespace {

bool sameShape(NSMenu* menu, const std::vector<MenuItem>& items) {
  if (static_cast<std::size_t>(menu.numberOfItems) != items.size()) return false;
  for (std::size_t i = 0; i < items.size(); ++i)
    if ([menu itemAtIndex:static_cast<NSInteger>(i)].separatorItem != items[i].separator) return false;
  return true;
}

void apply(NSMenuItem* entry, const MenuItem& item, std::size_t index, id target, SEL action) {
  NSString* title = nsText(item.text);
  if (![entry.title isEqualToString:title]) entry.title = title;
  entry.tag = static_cast<NSInteger>(index);
  const bool clickable = item.enabled && item.action != MenuAction::None;
  entry.target = clickable ? target : nil;
  entry.action = clickable ? action : nullptr;
  entry.enabled = item.enabled;
  entry.state = item.checkable && item.checked ? NSControlStateValueOn : NSControlStateValueOff;
}

}

void fillMenu(NSMenu* menu, const std::vector<MenuItem>& items, id target, SEL action) {
  menu.autoenablesItems = NO;
  if (!sameShape(menu, items)) {
    [menu removeAllItems];
    for (const MenuItem& item : items)
      [menu addItem:item.separator ? NSMenuItem.separatorItem
                                   : [[NSMenuItem alloc] initWithTitle:@"" action:nullptr keyEquivalent:@""]];
  }
  for (std::size_t i = 0; i < items.size(); ++i)
    if (!items[i].separator) apply([menu itemAtIndex:static_cast<NSInteger>(i)], items[i], i, target, action);
}

}
