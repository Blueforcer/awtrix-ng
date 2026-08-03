#include "core/script/SharedState.h"

namespace awtrix::script {
namespace {

bool keyChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
         c == '-';
}

}

SharedState::Value SharedState::Value::ofInt(int64_t v) {
  Value out;
  out.type = Type::Int;
  out.i = v;
  return out;
}

SharedState::Value SharedState::Value::ofReal(double v) {
  Value out;
  out.type = Type::Real;
  out.r = v;
  return out;
}

SharedState::Value SharedState::Value::ofBool(bool v) {
  Value out;
  out.type = Type::Bool;
  out.i = v ? 1 : 0;
  return out;
}

SharedState::Value SharedState::Value::ofStr(std::string v) {
  Value out;
  out.type = Type::Str;
  out.s = std::move(v);
  return out;
}

bool SharedState::validKey(const std::string& key) {
  if (key.empty() || key.size() > kMaxSharedKeyChars) return false;
  for (const char c : key)
    if (!keyChar(c)) return false;
  return true;
}

// Splits "owner.key" into its parts; a name with no dot reads the caller's own namespace.
// The owner is not checked against anything -- an unknown one just finds no value.
void SharedState::splitQualified(const std::string& qualified, const std::string& self,
                                 std::string& owner, std::string& key) {
  const std::size_t dot = qualified.rfind('.');
  if (dot == std::string::npos) {
    owner = self;
    key = qualified;
    return;
  }
  owner = qualified.substr(0, dot);
  key = qualified.substr(dot + 1);
}

SharedState::Status SharedState::set(const std::string& owner, const std::string& key, Value v,
                                     int64_t nowMs) {
  if (owner.empty() || !validKey(key)) return Status::InvalidKey;

  // The key being written is left out of `used`, so overwriting a value is measured against
  // its new size only. Otherwise a script at its budget could never shrink an entry.
  auto it = ns_.find(owner);
  std::size_t used = 0;
  if (it != ns_.end()) {
    bool replacing = false;
    for (const auto& kv : it->second) {
      if (kv.first == key) {
        replacing = true;
        continue;
      }
      used += kv.first.size() + kv.second.s.size();
    }
    if (!replacing && it->second.size() >= kMaxSharedKeysPerApp) return Status::KeyLimit;
  }

  if (used + key.size() + v.s.size() > kMaxSharedBytesPerApp) return Status::ByteLimit;

  v.writtenMs = nowMs;
  ns_[owner][key] = std::move(v);
  return Status::Ok;
}

void SharedState::erase(const std::string& owner, const std::string& key) {
  auto it = ns_.find(owner);
  if (it == ns_.end()) return;
  it->second.erase(key);
  if (it->second.empty()) ns_.erase(it);
}

const SharedState::Value* SharedState::find(const std::string& owner,
                                            const std::string& key) const {
  auto ns = ns_.find(owner);
  if (ns == ns_.end()) return nullptr;
  auto kv = ns->second.find(key);
  return kv == ns->second.end() ? nullptr : &kv->second;
}

std::vector<std::string> SharedState::keys(const std::string& owner) const {
  std::vector<std::string> out;
  for (const auto& ns : ns_) {
    if (!owner.empty() && ns.first != owner) continue;
    for (const auto& kv : ns.second) out.push_back(ns.first + "." + kv.first);
  }
  return out;
}

void SharedState::purge(const std::string& owner) { ns_.erase(owner); }

void SharedState::clear() { ns_.clear(); }

std::size_t SharedState::entries() const {
  std::size_t n = 0;
  for (const auto& ns : ns_) n += ns.second.size();
  return n;
}

std::vector<SharedEntry> snapshot(const SharedState& state, int64_t nowMs) {
  std::vector<SharedEntry> out;
  for (const auto& ns : state.all()) {
    for (const auto& kv : ns.second) {
      SharedEntry e;
      e.owner = ns.first;
      e.key = kv.first;
      e.value = kv.second;
      e.ageMs = nowMs - kv.second.writtenMs;
      out.push_back(std::move(e));
    }
  }
  return out;
}

std::size_t SharedState::bytes(const std::string& owner) const {
  auto it = ns_.find(owner);
  if (it == ns_.end()) return 0;
  std::size_t n = 0;
  for (const auto& kv : it->second) n += kv.first.size() + kv.second.s.size();
  return n;
}

}
