#pragma once

#import <AppKit/AppKit.h>

#include <string>

#include "platform/host/app/macos/HostDriver.h"
#include "platform/host/app/macos/StartAtLogin.h"

@interface HostStatusBar : NSObject <NSMenuDelegate>

- (instancetype)initWithDriver:(awtrix::hostapp::HostDriver*)driver
                         login:(awtrix::hostapp::StartAtLogin*)login
                       version:(const std::string&)version
                       logPath:(const std::string&)logPath;
- (void)refresh;

@end
