#include "platform/linux/ble/BleScripting.h"

#include <algorithm>

#include "berry.h"
#include "core/api/JsonReader.h"
#include "core/script/BerryVM.h"

namespace awtrix::ble {
namespace {

// The module scripts import. cbs holds each app's callbacks by request id (-id for a link's
// on_disconnect); the host reaches them through the three globals below, and _ble_cbs shows them.
constexpr const char* kSource = R"BERRY(
import json
import global

var m = module('ble')
var cbs = {}
global._ble_cbs = cbs
var next = 1

def on(id, fn)
  var app = _native_app()
  if !cbs.contains(app) cbs[app] = {} end
  cbs[app][id] = fn
end

def off(id)
  var mine = cbs.find(_native_app())
  if mine != nil && mine.contains(id) mine.remove(id) end
end

global._ble_dispatch = def (id, text, done)
  var mine = cbs.find(_native_app())
  if mine == nil return end
  var key = int(id)
  var fn = mine.find(key)
  if fn == nil return end
  if done == '1' mine.remove(key) end
  fn(text)
end

global._ble_forget = def (app, id)
  var mine = cbs.find(app)
  if mine == nil return end
  var key = int(id)
  if mine.contains(key) mine.remove(key) end
  if mine.contains(-key) mine.remove(-key) end
end

global._ble_drop = def (app, unused)
  if cbs.contains(app) cbs.remove(app) end
end

def hex(v)
  if v == nil return nil end
  if isinstance(v, bytes) return v.tohex() end
  if type(v) == 'string' return v end
  raise 'value_error', 'ble data must be bytes or a hex string'
end

def ask(op, args, id)
  var r = json.load(_native_ble(op, json.dump(args), id))
  return r == nil ? {'error': 'no answer'} : r
end

def failed(op, r)
  if !r.contains('error') return false end
  log('ble.' + op + ': ' + r['error'])
  return true
end

def start(op, args, handler)
  var id = next
  next += 1
  on(id, handler)
  if failed(op, ask(op, args, id))
    off(id)
    return nil
  end
  return id
end

def decode(e)
  for k : ['adv', 'rsp', 'data']
    if e.contains(k) e[k] = bytes(e[k]) end
  end
  if e.contains('mfg')
    for x : e['mfg'] x['data'] = bytes(x['data']) end
  end
  if e.contains('svc')
    for k : e['svc'].keys() e['svc'][k] = bytes(e['svc'][k]) end
  end
  return e
end

def flags(p)
  var out = ''
  if (p & 0x02) != 0 out += 'r' end
  if (p & 0x08) != 0 out += 'w' end
  if (p & 0x04) != 0 out += 'x' end
  if (p & 0x10) != 0 out += 'n' end
  if (p & 0x20) != 0 out += 'i' end
  return out
end

def once(op, args, cb, convert)
  return start(op, args, def (text)
    if cb == nil return end
    var e = json.load(text)
    if e.contains('error') cb(nil, e['error']) return end
    cb(convert(e), nil)
  end)
end

def stream(op, args, cb, convert)
  return start(op, args, def (text)
    var e = json.load(text)
    if e.contains('end') return end
    if e.contains('error') cb(nil, e['error']) return end
    cb(convert(e), nil)
  end)
end

def target(conn, svc, chr)
  return {'conn': conn, 'svc': svc, 'chr': chr}
end

def merged(args, opts)
  if opts != nil
    for k : opts.keys() args[k] = opts[k] end
  end
  return args
end

m.available = def () return !ask('state', {}, 0).contains('error') end
m.state = def () return ask('state', {}, 0).find('state', 'unavailable') end
m.address = def () return ask('state', {}, 0).find('addr') end

m.scan = def (cb, opts)
  return stream('scan', opts == nil ? {} : opts, cb, decode)
end

m.stop = def (handle) return !failed('stop', ask('stop', {'id': handle}, 0)) end

m.connect = def (addr, cb, opts)
  var id
  id = start('connect', merged({'addr': addr}, opts), def (text)
    var e = json.load(text)
    if e.contains('connected') cb(id, nil) return end
    if e.contains('disconnected')
      var closer = cbs.find(_native_app(), {}).find(-id)
      off(-id)
      if closer != nil closer(e.find('reason')) end
      return
    end
    if e.contains('error') cb(nil, e['error']) end
  end)
  return id
end

m.on_disconnect = def (conn, fn) on(-conn, fn) end
m.disconnect = def (conn) return !failed('disconnect', ask('disconnect', {'conn': conn}, 0)) end

m.services = def (conn)
  var r = ask('services', {'conn': conn}, 0)
  if failed('services', r) return nil end
  for s : r['services']
    for c : s['chars'] c['props'] = flags(c['props']) end
  end
  return r['services']
end

m.read = def (conn, svc, chr, cb)
  return once('read', target(conn, svc, chr), cb, def (e) return bytes(e['data']) end)
end

m.write = def (conn, svc, chr, data, cb, noresp)
  var args = target(conn, svc, chr)
  args['data'] = hex(data)
  args['noresp'] = noresp == true
  return once('write', args, cb, def (e) return true end)
end

m.subscribe = def (conn, svc, chr, cb, opts)
  return stream('subscribe', merged(target(conn, svc, chr), opts), cb, def (e) return bytes(e['data']) end)
end

m.pair = def (conn, cb) return once('pair', {'conn': conn}, cb, def (e) return true end) end
m.bonds = def () return ask('bonds', {}, 0).find('bonds', []) end
m.forget = def (addr) return !failed('forget', ask('forget', {'addr': addr}, 0)) end

m.advertise = def (opts)
  if opts == nil opts = {} end
  var args = {}
  for k : ['name', 'uuids', 'solicit', 'connectable']
    if opts.contains(k) args[k] = opts[k] end
  end
  if opts.contains('mfg')
    var list = []
    for id : opts['mfg'].keys() list.push({'id': id, 'data': hex(opts['mfg'][id])}) end
    args['mfg'] = list
  end
  if opts.contains('svc')
    var svc = {}
    for k : opts['svc'].keys() svc[k] = hex(opts['svc'][k]) end
    args['svc'] = svc
  end
  return start('advertise', args, def (text) end)
end

m.serve = def (uuid, chars, cb)
  var list = []
  for c : chars
    var entry = {'uuid': c['uuid'], 'props': c.find('props', 'r')}
    if c.contains('value') entry['value'] = hex(c['value']) end
    list.push(entry)
  end
  return start('serve', {'uuid': uuid, 'chars': list}, def (text)
    if cb == nil return end
    var e = json.load(text)
    if e.contains('end') return end
    if e.contains('data') e['data'] = bytes(e['data']) end
    cb(e)
  end)
end

m.set = def (service, chr, data)
  return !failed('set', ask('set', {'svc': service, 'chr': chr, 'data': hex(data)}, 0))
end

return m
)BERRY";

}

void BleScripting::install(script::ScriptExtensionHost& host) {
  host.defineNative("_native_ble", &BleScripting::native, this);
  host.defineModule("ble", kSource);
}

// _native_ble(op, args, id): the script's request as JSON, and the id its callback waits under.
int BleScripting::native(bvm* vm) {
  auto* self = static_cast<BleScripting*>(script::BerryVM::nativeSelf(vm));
  if (!self || be_top(vm) < 3 || !be_isstring(vm, 1) || !be_isstring(vm, 2) || !be_isint(vm, 3)) be_return_nil(vm);
  const std::string out = self->call(script::ScriptExtensionHost::caller(), be_tostring(vm, 1), be_tostring(vm, 2),
                                     static_cast<uint32_t>(be_toint(vm, 3)));
  be_pushstring(vm, out.c_str());
  be_return(vm);
}

std::string BleScripting::call(const std::string& app, const std::string& op, const std::string& args, uint32_t id) {
  const auto key = std::make_pair(app, id);
  if (id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_[key] = {};
  }
  std::string result = backend_.call(app, op, args, id);
  if (id && api::present(api::memberValue(api::JsonReader(result), "error"))) {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_.erase(key);
  }
  return result;
}

void BleScripting::push(BleEvent event) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto pending = pending_.find({event.script, event.id});
  if (pending == pending_.end()) return;
  if (event.done) pending->second.done = true;
  if (events_.size() >= kQueue) {
    const BleEvent& oldest = events_.front();
    if (oldest.done) {
      auto lost = pending_.find({oldest.script, oldest.id});
      if (lost != pending_.end()) lost->second.dropped = true;
    }
    events_.pop_front();
  }
  events_.push_back(std::move(event));
}

// The next event to hand over. A request whose last event was pushed out of a full queue still
// ends: its callback is dropped without being called.
bool BleScripting::pop(BleEvent& event, bool& deliver) {
  std::lock_guard<std::mutex> lock(mutex_);
  while (!events_.empty()) {
    event = std::move(events_.front());
    events_.pop_front();
    auto pending = pending_.find({event.script, event.id});
    if (pending == pending_.end()) continue;
    deliver = !pending->second.dropped;
    if (!deliver) event.done = true;
    if (event.done) pending_.erase(pending);
    return true;
  }
  for (auto it = pending_.begin(); it != pending_.end(); ++it) {
    if (!it->second.done) continue;
    event = {it->first.first, it->first.second, true, {}};
    pending_.erase(it);
    deliver = false;
    return true;
  }
  return false;
}

void BleScripting::tick(script::ScriptExtensionHost& host, const RenderCtx* ctx) {
  BleEvent event;
  bool deliver = false;
  while (pop(event, deliver)) {
    if (deliver)
      host.deliver(event.script, "ble callback", "_ble_dispatch", std::to_string(event.id), event.json,
                   event.done ? "1" : "0", ctx);
    if (event.done) host.call("_ble_forget", event.script, std::to_string(event.id));
  }
}

void BleScripting::forget(script::ScriptExtensionHost& host, const std::string& app) {
  backend_.forget(app);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = pending_.begin(); it != pending_.end();)
      it = it->first.first == app ? pending_.erase(it) : std::next(it);
    events_.erase(std::remove_if(events_.begin(), events_.end(), [&](const BleEvent& e) { return e.script == app; }),
                  events_.end());
  }
  host.call("_ble_drop", app, "");
}

}
