#include "platform/linux/layout/LayoutScripting.h"

#include "platform/linux/layout/LayoutModule.h"
#include "platform/linux/layout/ScriptLayoutBindings.h"

namespace awtrix::layout {

void LayoutScripting::install(script::ScriptExtensionHost& host) {
  host.defineNative("_native_layout_prepare", script::b_layout_prepare, &layouts_);
  host.defineNative("_native_layout_update", script::b_layout_update, &layouts_);
  host.defineNative("_native_layout_draw", script::b_layout_draw, &layouts_);
  host.defineNative("_native_layout_release", script::b_layout_release, &layouts_);
  host.defineModule("layout", kModule);
}

}
