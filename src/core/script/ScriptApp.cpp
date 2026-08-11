#include "core/script/ScriptApp.h"

#include <chrono>

#include "core/render/Canvas.h"
#include "core/render/TextRenderer.h"
#include "core/script/ScriptBindings.h"
#include "core/script/ScriptServices.h"

namespace awtrix::script {
namespace {

const char* const kHookNames[ScriptApp::kHookCount] = {
    "draw", "setup", "loop", "on_show", "on_hide", "on_button", "should_show", "duration",
};

}

ScriptApp::ScriptApp(BerryVM& vm, std::string name, const std::string& source,
                     const ScriptMeta& meta, const std::string& storeJson, const RenderCtx* ctx)
    : vm_(vm), name_(std::move(name)) {
  // Before the body runs, so both top-level code and setup() already see restored values.
  if (!storeJson.empty()) {
    BindingScope scope(nullptr, ctx, name_);
    vm_.call1("_store_load", storeJson);
  }

  {
    BindingScope scope(nullptr, ctx, name_);
    if (!vm_.loadApp(name_, source, kHookNames, kHookCount, hooks_)) {
      broken_ = true;
      error_ = parseScriptError(vm_.lastError());
      return;
    }
  }
  if (!meta.headless && !has(kDraw)) {
    broken_ = true;
    error_.message = "no draw() method";
    vm_.dropApp(name_);
    return;
  }

  if (has(kSetup)) {
    BindingScope scope(nullptr, ctx, name_);
    enter("setup", vm_.method(name_, "setup"));
  }
}

// One failing hook retires the whole app: broken_ latches and every later hook is skipped,
// so a script cannot throw once a frame forever. `what` names the hook for the error report.
void ScriptApp::enter(const char* what, bool okResult) {
  if (okResult) return;
  broken_ = true;
  error_ = parseScriptError(vm_.lastError(), what);
}

void ScriptApp::render(Canvas& canvas, const RenderCtx& ctx) {
  scroll_.beginFrame();
  if (broken_) {
    canvas.clear();
    if (ctx.font) text::drawText(canvas, *ctx.font, 0, 6, "ERR:" + name_, 0xFF0000u);
    return;
  }
  BindingScope scope(&canvas, &ctx, name_, &scroll_);
  const ScriptServices* svc = services();
  // Same call twice: the timed branch only exists because reading the clock around every
  // frame is not worth paying for unless someone is listening to the numbers.
  if (!svc || !svc->logDebug) {
    enter("draw", vm_.method(name_, "draw"));
    return;
  }
  const auto t0 = std::chrono::steady_clock::now();
  enter("draw", vm_.method(name_, "draw"));
  const long us = static_cast<long>(
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - t0)
          .count());
  if (us > peakDrawUs_) peakDrawUs_ = us;
}

void ScriptApp::tickLoop(const RenderCtx& ctx) {
  if (broken_ || !has(kLoop)) return;
  BindingScope scope(nullptr, &ctx, name_);
  enter("loop", vm_.method(name_, "loop"));
}

bool ScriptApp::wantsShow(const RenderCtx* ctx) {
  if (broken_ || !has(kShouldShow)) {
    lastWantShow_ = true;
    return true;
  }
  bool want = true;
  BindingScope scope(nullptr, ctx, name_);
  enter("should_show", vm_.methodBool(name_, "should_show", want));
  lastWantShow_ = broken_ ? true : want;
  return lastWantShow_;
}

void ScriptApp::notifyVisible(bool v, const RenderCtx* ctx) {
  if (v == visible_) return;
  visible_ = v;
  if (!v) scroll_.clear();
  if (broken_) return;
  const Hook h = v ? kOnShow : kOnHide;
  if (has(h)) {
    BindingScope scope(nullptr, ctx, name_);
    enter(kHookNames[h], vm_.method(name_, kHookNames[h]));
  }
  if (v) refreshDuration(ctx);
}

void ScriptApp::refreshDuration(const RenderCtx* ctx) {
  dwellMs_ = 0;
  if (broken_ || !has(kDuration)) return;
  long ms = 0;
  BindingScope scope(nullptr, ctx, name_);
  enter("duration", vm_.methodInt(name_, "duration", ms));
  if (!broken_ && ms > 0) dwellMs_ = ms;
}

void ScriptApp::handleButton(const std::string& btn, const RenderCtx* ctx) {
  if (broken_ || !visible_ || !has(kOnButton)) return;
  BindingScope scope(nullptr, ctx, name_);
  enter("on_button", vm_.method1(name_, "on_button", btn));
}

// Goes through the prelude's dispatcher rather than the app instance, because the callback
// is a closure the script registered. Arguments are stringified: BerryVM only passes strings.
void ScriptApp::dispatchHttp(uint32_t id, int status, const std::string& body, bool ok,
                             const RenderCtx* ctx) {
  if (broken_) return;
  BindingScope scope(nullptr, ctx, name_);
  if (ok)
    enter("http callback", vm_.call3("_dispatch_http_str", std::to_string(id),
                                     std::to_string(status), body));
  else
    enter("http callback",
          vm_.call2("_dispatch_http_fail", std::to_string(id), std::to_string(status)));
}

void ScriptApp::dispatchMqtt(const std::string& filter, const std::string& topic,
                             const std::string& payload, const RenderCtx* ctx) {
  if (broken_) return;
  BindingScope scope(nullptr, ctx, name_);
  enter("mqtt callback", vm_.call3("_dispatch_mqtt", filter, topic, payload));
}

bool ScriptApp::callCheckForTest(std::string& out) {
  BindingScope scope(nullptr, nullptr, name_);
  return vm_.methodString(name_, "check", out);
}

}
