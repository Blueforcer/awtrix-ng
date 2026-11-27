#pragma once

#import <AppKit/AppKit.h>

#include <vector>

#include "platform/host/app/StatusMenu.h"

namespace awtrix::hostapp {

void fillMenu(NSMenu* menu, const std::vector<MenuItem>& items, id target, SEL action);

}
