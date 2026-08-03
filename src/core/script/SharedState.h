#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace awtrix::script {

constexpr std::size_t kMaxSharedKeysPerApp = 8;
constexpr std::size_t kMaxSharedBytesPerApp = 256;
constexpr std::size_t kMaxSharedKeyChars = 24;

// The volatile noticeboard scripts publish to and read from each other. Scalars only: in one
// shared VM a map handed across would stay a single mutable object. Nothing survives a reboot.
class SharedState {
 public:
  enum class Type : uint8_t { Int, Real, Bool, Str };

  struct Value {
    Type type = Type::Int;
    int64_t i = 0;
    double r = 0.0;
    std::string s;
    int64_t writtenMs = 0;

    static Value ofInt(int64_t v);
    static Value ofReal(double v);
    static Value ofBool(bool v);
    static Value ofStr(std::string v);
  };

  enum class Status { Ok, InvalidKey, KeyLimit, ByteLimit };

  Status set(const std::string& owner, const std::string& key, Value v, int64_t nowMs);
  void erase(const std::string& owner, const std::string& key);
  const Value* find(const std::string& owner, const std::string& key) const;

  std::vector<std::string> keys(const std::string& owner = std::string()) const;

  void purge(const std::string& owner);
  void clear();

  std::size_t entries() const;
  std::size_t bytes(const std::string& owner) const;

  const std::map<std::string, std::map<std::string, Value>>& all() const { return ns_; }

  static bool validKey(const std::string& key);

  static void splitQualified(const std::string& qualified, const std::string& self,
                             std::string& owner, std::string& key);

 private:
  std::map<std::string, std::map<std::string, Value>> ns_;
};

struct SharedEntry {
  std::string owner;
  std::string key;
  SharedState::Value value;
  int64_t ageMs = 0;
};

std::vector<SharedEntry> snapshot(const SharedState& state, int64_t nowMs);

}
