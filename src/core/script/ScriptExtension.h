#pragma once

#include <string>
#include <vector>

typedef struct bvm bvm;

namespace awtrix {
class Canvas;
struct RenderCtx;
}

namespace awtrix::script {

// What the script host lends an extension: room for natives and modules, and a way to hand an
// app the results of its asynchronous requests.
class ScriptExtensionHost {
 public:
  using Native = int (*)(bvm*);

  virtual ~ScriptExtensionHost() = default;
  // A VM global `name` that runs fn; inside fn, BerryVM::nativeSelf(vm) returns `self`.
  virtual void defineNative(const char* name, Native fn, void* self) = 0;
  // Compiles source, which must end with `return <value>`, and files the value under the
  // import name `name`. False, with the error logged, when it does not load.
  virtual bool defineModule(const std::string& name, const std::string& source) = 0;
  // Calls the VM global `function` with three string arguments as a callback of `app`: with its
  // context, instruction budget and error reporting, which names it `what`. False when the app is
  // missing or not running.
  virtual bool deliver(const std::string& app, const char* what, const char* function, const std::string& a,
                       const std::string& b, const std::string& c, const RenderCtx* ctx) = 0;
  // Calls a registered app hook on the visible app. True when the hook consumes the event.
  virtual bool deliver(const std::string& app, const char* hook, const std::string& event,
                       const RenderCtx* ctx) = 0;
  // Calls the VM global `function` outside any app.
  virtual void call(const char* function, const std::string& a, const std::string& b) = 0;
  // Inside a native: the app whose code called it.
  static const std::string& caller();
  static Canvas* canvas();
  static const RenderCtx* context();
};

// A platform's addition to the script engine, such as a module only one kind of display has.
// The host installs it once, gives it a turn on every tick, and tells it when an app goes.
class ScriptExtension {
 public:
  virtual ~ScriptExtension() = default;
  // The import names it defines; scripts cannot install modules under them.
  virtual std::vector<std::string> modules() const = 0;
  virtual std::vector<std::string> hooks() const { return {}; }
  virtual void install(ScriptExtensionHost& host) = 0;
  virtual void tick(ScriptExtensionHost& host, const RenderCtx* ctx) {
    (void)host;
    (void)ctx;
  }
  virtual void forget(ScriptExtensionHost& host, const std::string& app) {
    (void)host;
    (void)app;
  }
  virtual void hidden(ScriptExtensionHost& host, const std::string& app) {
    (void)host;
    (void)app;
  }
  virtual void beginFrame(ScriptExtensionHost& host, const std::string& app) {
    (void)host;
    (void)app;
  }
  virtual bool holds(const std::string& app) const {
    (void)app;
    return false;
  }
};

// The optional lifecycle port owned by a platform's extension adapter.
class ScriptExtensionLifecycle {
 public:
  virtual ~ScriptExtensionLifecycle() = default;
  virtual bool reserves(const std::string& module) const = 0;
  virtual void tick(const RenderCtx* ctx) = 0;
  virtual void forget(const std::string& app) = 0;
  virtual void hidden(const std::string& app) = 0;
  virtual void beginFrame(const std::string& app) = 0;
  virtual bool holds(const std::string& app) const = 0;
};

}
