#include "platform/linux/secrets/SecretScripting.h"

#include "berry.h"
#include "core/script/BerryVM.h"

namespace awtrix::secrets {
namespace {

constexpr const char* kSource = R"BERRY(
var m = module('secret')
m.has = _secret_has
m.allows = _secret_allows
return m
)BERRY";

}

void SecretScripting::install(script::ScriptExtensionHost& host) {
  host.defineNative("_secret_has", &SecretScripting::has, &service_);
  host.defineNative("_secret_allows", &SecretScripting::allows, &service_);
  host.defineModule("secret", kSource);
}

int SecretScripting::has(bvm* vm) {
  const std::string& app = script::ScriptExtensionHost::caller();
  const bool set = !app.empty() && be_top(vm) >= 1 && be_isstring(vm, 1) &&
                   static_cast<Service*>(script::BerryVM::nativeSelf(vm))->has(app, be_tostring(vm, 1));
  be_pushbool(vm, set);
  be_return(vm);
}

int SecretScripting::allows(bvm* vm) {
  const std::string& app = script::ScriptExtensionHost::caller();
  const bool permitted = !app.empty() && be_top(vm) >= 2 && be_isstring(vm, 1) && be_isstring(vm, 2) &&
                         static_cast<Service*>(script::BerryVM::nativeSelf(vm))
                             ->allows(app, be_tostring(vm, 1), be_tostring(vm, 2));
  be_pushbool(vm, permitted);
  be_return(vm);
}

}
