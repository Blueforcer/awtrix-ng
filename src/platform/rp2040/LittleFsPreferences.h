#pragma once
#include <Arduino.h>
#include "persistence/KvPreferences.h"

namespace awtrix::storage { KvBackend& littleFsKvBackend(); }

// The Preferences subset used by AWTRIX. Unlike NVS, a namespace is committed
// atomically at end(), not once per put. Callers must explicitly end a write.
class Preferences {
 public:
  Preferences() : store_(awtrix::storage::littleFsKvBackend()) {}
  bool begin(const char* name, bool readOnly = false) { return store_.begin(name, readOnly); }
  void end();
  bool clear() { return store_.clear(); }
  bool isKey(const char* key) const { return store_.isKey(key); }
  bool remove(const char* key) { return store_.remove(key); }
  String getString(const char* key, const char* fallback = "") const {
    return String(store_.get(key, Store::String, fallback).c_str());
  }
  size_t putString(const char* key, const char* value) {
    return value ? store_.put(key, Store::String, value) : 0;
  }
#define NUMBER(Name, CppType, Kind, Width) \
  CppType get##Name(const char* k, CppType v = 0) const { \
    return static_cast<CppType>(store_.getNumber(k, Store::Kind, static_cast<uint32_t>(v))); \
  } \
  size_t put##Name(const char* k, CppType v) { \
    return store_.putNumber(k, Store::Kind, static_cast<uint32_t>(v), Width); \
  }
  NUMBER(Bool, bool, Bool, 1)
  NUMBER(Int, int32_t, Int, 4)
  NUMBER(Long, int32_t, Int, 4)
  NUMBER(UInt, uint32_t, UInt, 4)
  NUMBER(UChar, uint8_t, UChar, 1)
  NUMBER(UShort, uint16_t, UShort, 2)
#undef NUMBER
  float getFloat(const char* k, float v = 0) const {
    uint32_t bits; static_assert(sizeof(bits) == sizeof(v), "32-bit float required");
    std::memcpy(&bits, &v, sizeof(bits));
    bits = store_.getNumber(k, Store::Float, bits);
    std::memcpy(&v, &bits, sizeof(v)); return v;
  }
  size_t putFloat(const char* k, float v) {
    uint32_t bits; std::memcpy(&bits, &v, sizeof(bits));
    return store_.putNumber(k, Store::Float, bits, 4);
  }
 private:
  using Store = awtrix::storage::KvPreferences;
  Store store_;
};
