#include "core/script/ScriptHost.h"

namespace awtrix::script {
std::map<std::string, ScriptInfo> scriptInfo(const ScriptHost* host) {
  return host ? host->list() : std::map<std::string, ScriptInfo>{};
}
}