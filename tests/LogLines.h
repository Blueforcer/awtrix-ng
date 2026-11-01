#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

#include "core/api/JsonStream.h"
#include "system/Log.h"

namespace awtrix::test {

inline std::string logAfter(uint32_t after) {
  std::string text;
  api::JsonStream out([](void* context, const char* data, std::size_t size) {
    static_cast<std::string*>(context)->append(data, size);
  }, &text);
  logbuf::streamJsonAfter(after, out);
  return text;
}

inline uint32_t logMark() {
  return static_cast<uint32_t>(std::strtoul(logAfter(~0u).c_str() + std::strlen("{\"next\":"), nullptr, 10));
}

}
