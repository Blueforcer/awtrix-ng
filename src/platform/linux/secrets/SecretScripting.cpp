#include "platform/linux/secrets/SecretScripting.h"

#include "berry.h"
#include "core/script/BerryVM.h"

namespace awtrix::secrets {
namespace {

constexpr const char* kSource = R"BERRY(
var m = module('secret')
m.has = _secret_has
return m
)BERRY";

}

void SecretScripting::install(script::ScriptExtensionHost& host) {
  host.defineNative("_secret_has", &SecretScripting::has, &service_);
  host.defineModule("secret", kSource);
}

int SecretScripting::has(bvm* vm) {
  const std::string& app = script::ScriptExtensionHost::caller();
  const bool set = !app.empty() && be_top(vm) >= 1 && be_isstring(vm, 1) &&
                   static_cast<Service*>(script::BerryVM::nativeSelf(vm))->has(app, be_tostring(vm, 1));
  be_pushbool(vm, set);
  be_return(vm);
}

}
