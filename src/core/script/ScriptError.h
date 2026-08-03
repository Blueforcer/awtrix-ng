#pragma once

#include <string>

namespace awtrix::script {

struct ScriptError {
  std::string message;
  int line = 0;
  std::string hook;

  bool empty() const { return message.empty(); }
  void clear() {
    message.clear();
    line = 0;
    hook.clear();
  }
};

ScriptError parseScriptError(const std::string& raw, const char* hook = nullptr);

}
