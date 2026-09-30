#include "core/script/ScriptInfo.h"
#include "core/script/ScriptHeap.h"

namespace awtrix::script {
std::map<std::string, ScriptInfo> scriptInfo(const ScriptHost*) { return {}; }
namespace heap {
Info info() { return {"unavailable", 0, false}; }
std::size_t growthBudget() { return 0; }
}
}