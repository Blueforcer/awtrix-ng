#include "platform/linux/script/TcpScripting.h"

#include "berry.h"
#include <cstring>
#include "core/script/BerryVM.h"

namespace awtrix::linux_script {
namespace {

constexpr const char* kSource = R"BERRY(
import global
var m = module('tcp')
var cbs = {}

global._tcp_dispatch = def (id, kind, data)
  var mine = cbs.find(_native_app())
  if mine == nil return end
  var key = int(id)
  var fn = mine.find(key)
  if fn == nil return end
  if kind == 'close' mine.remove(key) end
  fn(kind, kind == 'open' ? nil : data)
end
global._tcp_drop = def (app, unused) if cbs.contains(app) cbs.remove(app) end end
global._tcp_drop_one = def (app, id)
  var mine = cbs.find(app)
  if mine != nil && mine.contains(int(id)) mine.remove(int(id)) end
end

m.connect = def (host, port, fn, opts)
  if type(host) != 'string' || size(host) == 0 raise 'value_error', 'tcp host must be a name or address' end
  port = int(port)
  if port < 1 || port > 65535 raise 'value_error', 'tcp port must be 1-65535' end
  if type(fn) != 'function' raise 'value_error', 'tcp.connect needs a callback' end
  var timeout = opts != nil ? int(opts.find('timeout', 10000)) : 10000
  if timeout < 1 || timeout > 60000 raise 'value_error', 'tcp timeout must be 1-60000 ms' end
  var id = _tcp_connect(host, port, timeout)
  if id == 0
    log('tcp.connect: too many connections')
    return nil
  end
  var app = _native_app()
  if !cbs.contains(app) cbs[app] = {} end
  cbs[app][id] = fn
  return id
end
m.send = def (c, text) return _tcp_send(c, str(text)) end
m.close = def (c)
  _tcp_close(c)
  var mine = cbs.find(_native_app())
  if mine != nil && mine.contains(c) mine.remove(c) end
end
return m
)BERRY";

TcpClients& clientsOf(bvm* vm) { return *static_cast<TcpClients*>(script::BerryVM::nativeSelf(vm)); }

}

void TcpScripting::install(script::ScriptExtensionHost& host) {
  host.defineNative("_tcp_connect", &TcpScripting::connect, &clients_);
  host.defineNative("_tcp_send", &TcpScripting::send, &clients_);
  host.defineNative("_tcp_close", &TcpScripting::close, &clients_);
  host.defineModule("tcp", kSource);
}

void TcpScripting::tick(script::ScriptExtensionHost& host, const RenderCtx* ctx) {
  static const char* const kKinds[] = {"open", "line", "close"};
  TcpEvent e;
  for (int i = 0; i < kPerTick && clients_.pop(e); ++i) {
    if (!host.deliver(e.app, "tcp", "_tcp_dispatch", std::to_string(e.id), kKinds[static_cast<int>(e.kind)], e.data,
                      ctx)) {
      clients_.close(e.app, e.id);
      host.call("_tcp_drop_one", e.app, std::to_string(e.id));
    }
  }
}

void TcpScripting::forget(script::ScriptExtensionHost& host, const std::string& app) {
  clients_.forget(app);
  host.call("_tcp_drop", app, "");
}

int TcpScripting::connect(bvm* vm) {
  if (!be_isstring(vm, 1) || be_strlen(vm, 1) < 1 || be_strlen(vm, 1) > 253 ||
      std::strlen(be_tostring(vm, 1)) != static_cast<size_t>(be_strlen(vm, 1)) ||
      !be_isint(vm, 2) || be_toint(vm, 2) < 1 || be_toint(vm, 2) > 65535 ||
      !be_isint(vm, 3) || be_toint(vm, 3) < 1 || be_toint(vm, 3) > 60000)
    be_raise(vm, "value_error", "invalid TCP endpoint or timeout");
  const std::string host = be_tostring(vm, 1);
  const auto port = static_cast<uint16_t>(be_toint(vm, 2));
  const int timeout = static_cast<int>(be_toint(vm, 3));
  be_pushint(vm, static_cast<bint>(clientsOf(vm).connect(script::ScriptExtensionHost::caller(), host, port, timeout)));
  be_return(vm);
}

int TcpScripting::send(bvm* vm) {
  const bool ok = be_isint(vm, 1) && be_isstring(vm, 2) &&
                  clientsOf(vm).send(script::ScriptExtensionHost::caller(), static_cast<uint32_t>(be_toint(vm, 1)),
                                     std::string(be_tostring(vm, 2), static_cast<size_t>(be_strlen(vm, 2))));
  be_pushbool(vm, ok);
  be_return(vm);
}

int TcpScripting::close(bvm* vm) {
  if (be_isint(vm, 1)) clientsOf(vm).close(script::ScriptExtensionHost::caller(), static_cast<uint32_t>(be_toint(vm, 1)));
  be_return_nil(vm);
}

}
