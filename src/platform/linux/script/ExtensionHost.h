#pragma once

#include "core/script/ScriptExtension.h"

namespace awtrix::script {

class ScriptHost;

// Owns a platform's extension registrations; the script host and extensions outlive this adapter.
class ExtensionHost final : public ScriptExtensionHost, private ScriptExtensionLifecycle {
 public:
  ExtensionHost(ScriptHost& host, std::vector<ScriptExtension*> extensions);
  ~ExtensionHost() override;
  ExtensionHost(const ExtensionHost&) = delete;
  ExtensionHost& operator=(const ExtensionHost&) = delete;

  void defineNative(const char* name, Native fn, void* self) override;
  bool defineModule(const std::string& name, const std::string& source) override;
  bool deliver(const std::string& app, const char* what, const char* function, const std::string& a,
               const std::string& b, const std::string& c, const RenderCtx* ctx) override;
  bool deliver(const std::string& app, const char* hook, const std::string& event,
               const RenderCtx* ctx) override;
  void call(const char* function, const std::string& a, const std::string& b) override;

 private:
  bool reserves(const std::string& module) const override;
  void tick(const RenderCtx* ctx) override;
  void forget(const std::string& app) override;
  void hidden(const std::string& app) override;
  void beginFrame(const std::string& app) override;
  bool holds(const std::string& app) const override;

  ScriptHost& host_;
  std::vector<ScriptExtension*> extensions_;
  std::vector<std::string> modules_;
  std::vector<std::string> hooks_;
};

}
