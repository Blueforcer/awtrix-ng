#pragma once

#include <algorithm>
#include <cstring>
#include <string_view>
#include "core/Command.h"

namespace awtrix::layout {
// Diagnostic paths are bounded independently of user input and never allocate while parsing.
class DetailPath {
 public:
  DetailPath(const char* value) : DetailPath(std::string_view(value)) {}
  DetailPath(std::string_view value) { append(value); }
  __attribute__((noinline)) DetailPath operator+(std::string_view suffix) const {
    DetailPath copy = *this; copy.append(suffix); return copy;
  }
  std::string_view view() const { return {bytes_, size_}; }
 private:
  __attribute__((noinline)) void append(std::string_view value) {
    const auto count = std::min(value.size(), sizeof(bytes_) - size_);
    std::memcpy(bytes_ + size_, value.data(), count); size_ += count;
  }
  char bytes_[96] = {};
  std::size_t size_ = 0;
};

inline bool memoryFailure(DispatchDetail& error) {
  // Short literals: reporting needs no allocation.
  error.field = "layout";
  error.message = "out of memory";
  return false;
}
inline bool layoutFailure(DispatchDetail& error, const DetailPath& field, const char* message) {
  error.field.assign(field.view().data(), field.view().size());
  error.message = message;
  return false;
}
}
