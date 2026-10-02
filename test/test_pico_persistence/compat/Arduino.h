#pragma once
#include <cstdint>
#include <string>
class String {
 public:
  String(const char* s = "") : value_(s) {}
  const char* c_str() const { return value_.c_str(); }
  std::size_t length() const { return value_.size(); }
  bool isEmpty() const { return value_.empty(); }
 private:
  std::string value_;
};
