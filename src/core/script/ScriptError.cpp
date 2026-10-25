#include "core/script/ScriptError.h"

#include <cctype>
#include <cstddef>

namespace awtrix::script {
namespace {

constexpr int kMaxLine = 99999;

// Finds "<chunk>:NN:" in e.message, strips it and keeps NN as the line.
bool takeLine(ScriptError& e, const std::string& prefix) {
  for (std::size_t at = e.message.find(prefix); at != std::string::npos;
       at = e.message.find(prefix, at + 1)) {
    const std::size_t first = at + prefix.size();
    std::size_t p = first;
    int line = 0;
    while (p < e.message.size() && std::isdigit(static_cast<unsigned char>(e.message[p]))) {
      if (line <= kMaxLine) line = line * 10 + (e.message[p] - '0');
      ++p;
    }
    if (p == first || p >= e.message.size() || e.message[p] != ':') continue;
    if (line <= 0 || line > kMaxLine) continue;

    ++p;
    if (p < e.message.size() && e.message[p] == ' ') ++p;
    e.message.erase(at, p - at);
    e.line = line;
    return true;
  }
  return false;
}

}

// Lifts the line number out of a Berry error and strips the "script:NN:" or "module:NN:" chunk
// prefix, so the UI can point at a line. Anything not shaped like that prefix is left exactly as
// it came.
ScriptError parseScriptError(const std::string& raw, const char* hook) {
  ScriptError e;
  e.message = raw;
  if (hook && *hook) e.hook = hook;

  if (!takeLine(e, std::string(kAppChunk) + ':')) takeLine(e, std::string(kModuleChunk) + ':');
  return e;
}

}
