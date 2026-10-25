#pragma once

#include <string>

namespace awtrix::script {

// The chunk names Berry compiles from. Errors locate a line as "<chunk>:NN:", and a runtime
// error reports the line in the chunk that ran, so a call into a module points at the caller.
constexpr const char* kAppChunk = "script";
constexpr const char* kModuleChunk = "module";

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
