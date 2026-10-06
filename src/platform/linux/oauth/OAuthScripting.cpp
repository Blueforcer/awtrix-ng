#include "platform/linux/oauth/OAuthScripting.h"

#include "berry.h"
#include "core/api/JsonReader.h"
#include "core/script/BerryVM.h"

namespace awtrix::oauth {
namespace {

// Every function the module calls is held in a local when it loads, so a script that later replaces
// a global or a member of this module or of json cannot change which app a request is filed under
// or what it sends. Results come back through the global _oauth_dispatch, which the host calls by
// name; tokens never enter the VM at all.
constexpr const char* kSource = R"BERRY(
import global
import json
var m = module('oauth')
var cbs = {}
var native_request = _oauth_request
var native_ready = _oauth_ready
var native_app = _native_app
var dump = json.dump

global._oauth_dispatch = def (id, status, body)
  var mine = cbs.find(native_app())
  if mine == nil return end
  var key = int(id)
  var fn = mine.find(key)
  if fn == nil return end
  mine.remove(key)
  var code = int(status)
  var empty = code == 0 || (code == 401 && size(body) == 0)
  fn(empty ? nil : body, code)
end
global._oauth_drop = def (app, unused) if cbs.contains(app) cbs.remove(app) end end

var send = def (method, url, body, fn, opts)
  if type(fn) != 'function' raise 'value_error', 'oauth needs a callback' end
  var headers = '{}'
  var cap = 8192
  if opts != nil
    var h = opts.find('headers')
    if h != nil headers = dump(h) end
    cap = int(opts.find('cap', cap))
  end
  var id = native_request(str(method), str(url), body == nil ? '' : str(body), headers, cap)
  if id == 0 return false end
  var app = native_app()
  if !cbs.contains(app) cbs[app] = {} end
  cbs[app][id] = fn
  return true
end
m.request = send
m.get = def (url, fn, opts) return send('GET', url, nil, fn, opts) end
m.post = def (url, body, fn, opts) return send('POST', url, body, fn, opts) end
m.ready = def () return native_ready() end
return m
)BERRY";

Service& serviceOf(bvm* vm) { return *static_cast<Service*>(script::BerryVM::nativeSelf(vm)); }

}

void OAuthScripting::install(script::ScriptExtensionHost& host) {
  host.defineNative("_oauth_request", &OAuthScripting::request, &service_);
  host.defineNative("_oauth_ready", &OAuthScripting::ready, &service_);
  host.defineModule("oauth", kSource);
}

void OAuthScripting::tick(script::ScriptExtensionHost& host, const RenderCtx* ctx) {
  Result r;
  for (int i = 0; i < kPerTick && service_.pop(r); ++i)
    if (!host.deliver(r.app, "oauth", "_oauth_dispatch", std::to_string(r.id), std::to_string(r.status), r.body, ctx))
      host.call("_oauth_drop", r.app, "");
}

void OAuthScripting::forget(script::ScriptExtensionHost& host, const std::string& app) {
  service_.forget(app);
  host.call("_oauth_drop", app, "");
}

namespace {

// be_raise leaves by longjmp, past every destructor, so all C++ objects live in here.
bool queue(bvm* vm, uint32_t& id) {
  const std::string_view json(be_tostring(vm, 4), static_cast<size_t>(be_strlen(vm, 4)));
  if (json.size() > 8192 || !api::isWellFormed(json)) return false;
  api::JsonReader r(json);
  if (!r.isObject() || !r.enterObject()) return false;
  Headers headers;
  while (r.nextMember()) {
    const api::JsonReader value = r;
    std::string text;
    if (!value.isString() || !value.appendString(text)) return false;
    headers.emplace_back(std::string(r.key()), std::move(text));
    if (!r.skipValue()) return false;
  }
  const bint cap = be_toint(vm, 5);
  id = serviceOf(vm).request(script::ScriptExtensionHost::caller(), be_tostring(vm, 1), be_tostring(vm, 2),
                             std::string(be_tostring(vm, 3), static_cast<size_t>(be_strlen(vm, 3))), headers,
                             static_cast<std::size_t>(cap < 1 ? 1 : cap));
  return true;
}

}

int OAuthScripting::request(bvm* vm) {
  if (!be_isstring(vm, 1) || !be_isstring(vm, 2) || !be_isstring(vm, 3) || !be_isstring(vm, 4) || !be_isint(vm, 5))
    be_raise(vm, "value_error", "invalid oauth request");
  uint32_t id = 0;
  if (!queue(vm, id)) be_raise(vm, "value_error", "oauth headers must be a map of strings");
  be_pushint(vm, static_cast<bint>(id));
  be_return(vm);
}

int OAuthScripting::ready(bvm* vm) {
  be_pushbool(vm, serviceOf(vm).ready(script::ScriptExtensionHost::caller()));
  be_return(vm);
}

}
