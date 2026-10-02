#pragma once

#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <utility>

namespace awtrix::storage {

// Portable, bounded namespace snapshot. Backend replacement must be atomic: a failed
// write leaves the previous snapshot intact. No Arduino or filesystem types here.
class KvBackend {
 public:
  enum class Read { Missing, Ok, Error };
  virtual ~KvBackend() = default;
  virtual Read read(const std::string& name, std::string& bytes) = 0;
  virtual bool replace(const std::string& name, const std::string& bytes) = 0;
};

class KvPreferences {
 public:
  static constexpr std::size_t kMaxBytes = 16384;
  enum Type : uint8_t { Bool = 1, Int, UInt, Float, UChar, UShort, String };
  explicit KvPreferences(KvBackend& backend) : backend_(backend) {}
  static bool validName(const std::string& s) {
    if (s.empty() || s.size() > 15) return false;
    for (unsigned char c : s)
      if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    return true;
  }
  bool begin(const char* name, bool readOnly = false) {
    if (open_ || !name || !validName(name)) return false;
    values_.clear();
    std::string bytes;
    const auto result = backend_.read(name, bytes);
    if (result == KvBackend::Read::Error ||
        (result == KvBackend::Read::Ok && !decode(bytes)) ||
        (result == KvBackend::Read::Missing && readOnly)) return false;
    name_ = name;
    readOnly_ = readOnly;
    open_ = true;
    dirty_ = false;
    return true;
  }
  // Explicit namespace transaction: all DeviceConfig fields land together at end().
  bool end() {
    const bool ok = !dirty_ || backend_.replace(name_, encode());
    open_ = dirty_ = false;
    values_.clear();
    return ok;
  }
  bool isKey(const char* key) const { return open_ && key && values_.count(key); }
  bool remove(const char* key) {
    if (!open_ || readOnly_ || !key || !values_.erase(key)) return false;
    dirty_ = true;
    return true;
  }
  bool clear() {
    if (!open_ || readOnly_) return false;
    values_.clear(); dirty_ = true; return true;
  }
  std::string get(const char* key, Type type, const std::string& fallback = {}) const {
    if (!open_ || !key) return fallback;
    auto i = values_.find(key);
    return i != values_.end() && i->second.first == type ? i->second.second : fallback;
  }
  std::size_t put(const char* key, Type type, const std::string& bytes) {
    if (!open_ || readOnly_ || !key || !validName(key) || !validValue(type, bytes)) return 0;
    auto old = values_.find(key);
    if (old != values_.end() && old->second == Value(type, bytes)) return bytes.size();
    std::size_t size = 8 + 4;
    for (const auto& v : values_) if (v.first != key) size += 4 + v.first.size() + v.second.second.size();
    if (size + 4 + std::strlen(key) + bytes.size() > kMaxBytes) return 0;
    values_[key] = {type, bytes}; dirty_ = true; return bytes.size();
  }
  uint32_t getNumber(const char* key, Type type, uint32_t fallback) const {
    const auto bytes = get(key, type);
    if (bytes.empty() || bytes.size() > 4 || type == String) return fallback;
    uint32_t value = 0;
    for (std::size_t i = 0; i < bytes.size(); ++i)
      value |= uint32_t(static_cast<uint8_t>(bytes[i])) << (8 * i);
    return value;
  }
  std::size_t putNumber(const char* key, Type type, uint32_t value, unsigned width) {
    if (width == 0 || width > 4 || type == String) return 0;
    std::string bytes;
    for (unsigned i = 0; i < width; ++i) bytes += char(value >> (8 * i));
    return put(key, type, bytes);
  }
 private:
  using Value = std::pair<Type, std::string>;
  static bool validValue(Type t, const std::string& b) {
    switch (t) {
      case Bool: return b.size() == 1 && uint8_t(b[0]) <= 1;
      case UChar: return b.size() == 1;
      case UShort: return b.size() == 2;
      case Int: case UInt: case Float: return b.size() == 4;
      case String: return b.size() <= kMaxBytes && b.find('\0') == std::string::npos;
      default: return false;
    }
  }
  static uint32_t crc(const std::string& b, std::size_t n) {
    uint32_t c = 0xffffffffu;
    for (std::size_t i = 0; i < n; ++i) {
      c ^= uint8_t(b[i]);
      for (int bit = 0; bit < 8; ++bit) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1)));
    }
    return ~c;
  }
  std::string encode() const {
    std::string b("AXKV\1\0\0\0", 8);
    for (const auto& v : values_) {
      b += char(v.first.size()); b += char(v.second.first);
      b += char(v.second.second.size()); b += char(v.second.second.size() >> 8);
      b += v.first; b += v.second.second;
    }
    const auto c = crc(b, b.size());
    for (int i = 0; i < 4; ++i) b += char(c >> (8 * i));
    return b;
  }
  bool decode(const std::string& b) {
    if (b.size() < 12 || b.size() > kMaxBytes || b.compare(0, 8, std::string("AXKV\1\0\0\0", 8))) return false;
    const auto end = b.size() - 4;
    uint32_t check = 0;
    for (int i = 0; i < 4; ++i) check |= uint32_t(uint8_t(b[end + i])) << (8 * i);
    if (check != crc(b, end)) return false;
    std::map<std::string, Value> parsed;
    for (std::size_t at = 8; at < end;) {
      if (end - at < 4) return false;
      const auto keyLen = uint8_t(b[at++]);
      const auto type = static_cast<Type>(uint8_t(b[at++]));
      const auto len = uint8_t(b[at]) | (unsigned(uint8_t(b[at + 1])) << 8); at += 2;
      if (keyLen > end - at || len > end - at - keyLen) return false;
      const auto key = b.substr(at, keyLen); at += keyLen;
      const auto value = b.substr(at, len); at += len;
      if (!validName(key) || !validValue(type, value) || parsed.count(key)) return false;
      parsed[key] = {type, value};
    }
    values_ = std::move(parsed);
    return true;
  }
  KvBackend& backend_;
  std::map<std::string, Value> values_;
  std::string name_;
  bool open_ = false, readOnly_ = true, dirty_ = false;
};
}
