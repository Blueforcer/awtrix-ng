#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "core/apps/IApp.h"
#include "core/script/BerryVM.h"
#include "core/script/ScriptError.h"
#include "core/script/ScriptMeta.h"
#include "core/script/ScrollBank.h"

namespace awtrix::script {

class ScriptApp : public IApp {
 public:
  // Order must match kHookNames in the .cpp: the enumerator doubles as the bit index into
  // hooks_, which loadApp() fills from that same name array.
  enum Hook : uint32_t {
    kDraw = 0,
    kSetup,
    kLoop,
    kOnShow,
    kOnHide,
    kOnButton,
    kShouldShow,
    kDuration,
    kHookCount,
  };

  ScriptApp(BerryVM& vm, std::string name, const std::string& source, const ScriptMeta& meta,
            const std::string& storeJson, const RenderCtx* ctx);

  const std::string& id() const override { return name_; }
  void render(Canvas& canvas, const RenderCtx& ctx) override;

  void tickLoop(const RenderCtx& ctx);
  void holdLoopUntil(int64_t ms) { loopNotBeforeMs_ = ms; }
  int64_t loopNotBeforeMs() const { return loopNotBeforeMs_; }
  bool wantsShow(const RenderCtx* ctx);
  bool lastWantedShow() const { return lastWantShow_; }
  long dwellMs() const { return dwellMs_; }
  void notifyVisible(bool visible, const RenderCtx* ctx);
  void handleButton(const std::string& btn, const RenderCtx* ctx);
  void dispatchHttp(uint32_t id, int status, const std::string& body, bool ok,
                    const RenderCtx* ctx);
  void dispatchMqtt(const std::string& filter, const std::string& topic,
                    const std::string& payload, const RenderCtx* ctx);

  long takePeakDrawUs() {
    const long p = peakDrawUs_;
    peakDrawUs_ = 0;
    return p;
  }

  bool ok() const { return !broken_; }
  const ScriptError& error() const { return error_; }
  bool visible() const { return visible_; }
  bool scrollHolds() const { return scroll_.wantsMoreTime(); }

  bool callCheckForTest(std::string& out);

 private:
  bool has(Hook h) const { return (hooks_ & (1u << h)) != 0; }
  void enter(const char* what, bool okResult);
  void refreshDuration(const RenderCtx* ctx);

  // Shared with every other app on the device; this class owns nothing but its entry in the
  // VM's app registry, keyed by name_.
  BerryVM& vm_;
  std::string name_;
  uint32_t hooks_ = 0;
  int64_t loopNotBeforeMs_ = 0;
  long peakDrawUs_ = 0;
  bool broken_ = false;
  bool visible_ = false;
  bool lastWantShow_ = true;
  long dwellMs_ = 0;
  ScriptError error_;
  ScrollBank scroll_;
};

}
