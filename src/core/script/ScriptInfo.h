#pragma once
#include <map>
#include <string>
#include "core/script/ScriptError.h"

namespace awtrix::script {
class ScriptHost;
// Snapshot-only seam: state serialization must not import the Berry VM or its queues.
struct ScriptInfo {
  ScriptError error;
  bool skipping = false;
  bool headless = false;
  bool module = false;
  bool config = false;
  std::string importName;
  std::string metaName;
  std::string desc;
  std::string author;
  std::string version;
  std::string icons;
};
std::map<std::string, ScriptInfo> scriptInfo(const ScriptHost* host);
}