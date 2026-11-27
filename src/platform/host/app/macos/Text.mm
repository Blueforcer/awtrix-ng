#include "platform/host/app/macos/Text.h"

namespace awtrix::hostapp {

NSString* nsText(const std::string& text) {
  NSString* converted = [[NSString alloc] initWithBytes:text.data() length:text.size() encoding:NSUTF8StringEncoding];
  return converted ? converted : @"";
}

std::string utf8(NSString* text) {
  const char* bytes = text.UTF8String;
  return bytes ? std::string(bytes) : std::string();
}

}
