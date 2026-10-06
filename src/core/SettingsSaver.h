#pragma once

#include <cstdint>

namespace awtrix {

class SettingsSaver {
 public:
  explicit constexpr SettingsSaver(int64_t lastSaveMs = 0) : lastSaveMs_(lastSaveMs) {}
  bool due(int64_t nowMs) const { return nowMs - lastSaveMs_ >= 1500; }
  void saved(int64_t nowMs) { lastSaveMs_ = nowMs; }

 private:
  int64_t lastSaveMs_;
};

}
