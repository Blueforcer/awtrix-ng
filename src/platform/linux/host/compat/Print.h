#pragma once


#include <cstddef>
#include <cstdint>
#include <cstring>

class Print {
 public:
  virtual ~Print() = default;

  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t* buffer, size_t size) {
    size_t n = 0;
    while (size--) {
      if (write(*buffer++) != 1) break;
      ++n;
    }
    return n;
  }

  size_t write(const char* str) {
    return str ? write(reinterpret_cast<const uint8_t*>(str), std::strlen(str)) : 0;
  }
  size_t write(const char* buffer, size_t size) {
    return write(reinterpret_cast<const uint8_t*>(buffer), size);
  }
};
