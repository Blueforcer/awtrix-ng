#include "platform/linux/script/ExtensionHost.h"

#include <algorithm>
#include <utility>

#include "core/script/ScriptBindings.h"
#include "core/script/ScriptHost.h"

namespace awtrix::script {

ExtensionHost::ExtensionHost(ScriptHost& host, std::vector<ScriptExtension*> extensions)
    : host_(host), extensions_(std::move(extensions)) {
  for (ScriptExtension* extension : extensions_) {
    for (auto& name : extension->modules()) modules_.push_back(std::move(name));
    for (auto& name : extension->hooks()) hooks_.push_back(std::move(name));
    if (host_.ready()) extension->install(*this);
  }
  host_.setExtensionLifecycle(this);
}

ExtensionHost::~ExtensionHost() { host_.setExtensionLifecycle(nullptr); }

void ExtensionHost::defineNative(const char* name, Native fn, void* self) {
  host_.defineNative(name, fn, self);
}

bool ExtensionHost::defineModule(const std::string& name, const std::string& source) {
  return host_.defineModule(name, source);
}

bool ExtensionHost::deliver(const std::string& app, const char* what, const char* function, const std::string& a,
                             const std::string& b, const std::string& c, const RenderCtx* ctx) {
  return host_.deliver(app, what, function, a, b, c, ctx);
}

bool ExtensionHost::deliver(const std::string& app, const char* hook, const std::string& event,
                             const RenderCtx* ctx) {
  if (!hook || std::find(hooks_.begin(), hooks_.end(), hook) == hooks_.end()) return false;
  return host_.deliverHook(app, hook, event, ctx);
}

void ExtensionHost::call(const char* function, const std::string& a, const std::string& b) {
  host_.call(function, a, b);
}

bool ExtensionHost::reserves(const std::string& module) const {
  return std::find(modules_.begin(), modules_.end(), module) != modules_.end();
}

void ExtensionHost::tick(const RenderCtx* ctx) {
  for (ScriptExtension* extension : extensions_) extension->tick(*this, ctx);
}

void ExtensionHost::forget(const std::string& app) {
  for (ScriptExtension* extension : extensions_) extension->forget(*this, app);
}

void ExtensionHost::hidden(const std::string& app) {
  for (ScriptExtension* extension : extensions_) extension->hidden(*this, app);
}

bool ExtensionHost::holds(const std::string& app) const {
  for (const ScriptExtension* extension : extensions_)
    if (extension->holds(app)) return true;
  return false;
}

void ExtensionHost::beginFrame(const std::string& app) {
  for (ScriptExtension* extension : extensions_) extension->beginFrame(*this, app);
}

const std::string& ScriptExtensionHost::caller() { return BindingScope::currentScript(); }
Canvas* ScriptExtensionHost::canvas() { return BindingScope::currentCanvas(); }
const RenderCtx* ScriptExtensionHost::context() { return BindingScope::currentContext(); }

}
