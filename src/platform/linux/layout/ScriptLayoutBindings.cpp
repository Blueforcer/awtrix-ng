#include "platform/linux/layout/ScriptLayoutBindings.h"

#include <string>

#include "berry.h"
#include "core/apps/IApp.h"
#include "platform/linux/layout/ScriptLayouts.h"
#include "core/script/ScriptExtension.h"
#include "core/script/BerryVM.h"

namespace awtrix::script {
namespace {

int32_t handle(bvm* vm) {
  return be_top(vm) >= 1 && be_isint(vm, 1) ? static_cast<int32_t>(be_toint(vm, 1)) : 0;
}

ScriptLayouts* store(bvm* vm) {
  return static_cast<ScriptLayouts*>(BerryVM::nativeSelf(vm));
}
}

int b_layout_prepare(bvm* vm) {
  std::string error;
  int32_t id = 0;
  if (store(vm) && be_top(vm) >= 1 && be_isstring(vm, 1))
    id = store(vm)->prepare(ScriptExtensionHost::caller(), be_tostring(vm, 1), error);
  if (id) be_pushint(vm, id);
  else be_pushstring(vm, error.empty() ? "layout service unavailable" : error.c_str());
  be_return(vm);
}

int b_layout_update(bvm* vm) {
  std::string error;
  const bool ok = store(vm) && be_top(vm) >= 2 && be_isstring(vm, 2) &&
      store(vm)->update(ScriptExtensionHost::caller(), handle(vm), be_tostring(vm, 2), error);
  if (ok) be_pushbool(vm, true);
  else be_pushstring(vm, error.empty() ? "layout.update needs handle and spec" : error.c_str());
  be_return(vm);
}

int b_layout_draw(bvm* vm) {
  std::string error;
  bool finished = false;
  bool ok = false;
  auto* canvas = ScriptExtensionHost::canvas();
  const auto* context = ScriptExtensionHost::context();
  if (canvas && store(vm)) {
    layout::FrameContext frame;
    const Settings* settings = context ? context->settings : nullptr;
    frame.nowMs = context ? context->nowMs : 0;
    if (settings) {
      frame.defaultColor = settings->textColor;
      frame.scrollDefaults = settings->scrollDefaults;
      frame.uppercase = settings->uppercase;
    }
    ok = store(vm)->draw(ScriptExtensionHost::caller(), handle(vm), *canvas, frame, finished, error);
  }
  if (ok) be_pushbool(vm, finished);
  else be_pushstring(vm, error.empty() ? "layout.draw only in draw()" : error.c_str());
  be_return(vm);
}

int b_layout_release(bvm* vm) {
  be_pushbool(vm, store(vm) && store(vm)->release(ScriptExtensionHost::caller(), handle(vm)));
  be_return(vm);
}

}
