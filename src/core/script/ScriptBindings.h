#pragma once

#include <string>

namespace awtrix {
class Canvas;
struct RenderCtx;
}

namespace awtrix::script {

class BerryVM;
class ScrollBank;
struct ScriptServices;

bool installBindings(BerryVM& vm, std::string& err);

void setServices(const ScriptServices* services);

const ScriptServices* services();

class BindingScope {
 public:
  BindingScope(Canvas* canvas, const RenderCtx* ctx, const std::string& scriptName,
               ScrollBank* scroll = nullptr);
  ~BindingScope();
  BindingScope(const BindingScope&) = delete;
  BindingScope& operator=(const BindingScope&) = delete;

  struct StoreFlush {
    std::string script;
    std::string json;
  };

  static bool storeFlushPending();
  static StoreFlush takeStoreFlush();

  static const std::string& currentScript();
};

}
