#pragma once

#include "core/script/IScriptFiles.h"
#include "core/script/ScriptHost.h"

namespace awtrix::script {

template <class Refused>
void restoreScripts(ScriptHost& scripts, IScriptFiles& files, int64_t staggerMs, Refused refused) {
  for (const bool modules : {true, false}) {
    files.loadAll([&](const std::string& name, const std::string& source, const std::string& state) {
      if (parseMeta(source).module != modules) return;
      if (!scripts.set(name, source, state)) refused(name, scripts.lastRefusal());
    });
  }
  if (scripts.count() && staggerMs > 0) scripts.staggerFirstLoops(staggerMs);
}

}
