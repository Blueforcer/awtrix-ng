#pragma once

#import <Foundation/Foundation.h>

#include <string>

namespace awtrix::hostapp {

NSString* nsText(const std::string& text);
std::string utf8(NSString* text);

}
