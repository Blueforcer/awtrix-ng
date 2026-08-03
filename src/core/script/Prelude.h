#pragma once

#include "core/script/ScriptServices.h"

namespace awtrix::script {

#define AWTRIX_PRELUDE_STR_(x) #x
#define AWTRIX_PRELUDE_STR(x) AWTRIX_PRELUDE_STR_(x)

inline const char* kPrelude = R"BERRY(
import json

# ---- http ------------------------------------------------------------------
# One shared request-id counter for the whole VM, so an id is unique across
# every app and the host can use it directly as the transport id -- no per-VM
# remap layer any more. Each pending request remembers WHICH app owns it, so
# _app_forget can drop an app's in-flight callbacks when it is removed.
http = module('http')
_http_cbs = {}
_http_next = 1

# Headers travel to C as one block of "Name: value" entries joined by \x1f. C
# parses and validates it -- one implementation shared by the device and the
# simulator, and the only place that knows the caps. The separator is a control
# character precisely because a header value may not contain one: a value with an
# embedded newline is caught there instead of quietly becoming a second header.
def _http_headers(opts)
  if opts == nil return "" end
  var h = opts.find('headers')
  if h == nil return "" end
  var out = ""
  for k : h.keys()
    if size(out) > 0 out += "\x1f" end
    out += str(k) + ": " + str(h[k])
  end
  return out
end

# The optional response filter: 'find' names a needle, 'keep' a window size.
# The transport then delivers `keep` bytes starting at the needle's first
# occurrence instead of the first 8 KB of the body -- which also reaches
# fields sitting past that cap. C validates both; here they only travel.
def _http_find(opts)
  if opts == nil return "" end
  var f = opts.find('find')
  return f == nil ? "" : str(f)
end

def _http_keep(opts)
  if opts == nil return 0 end
  var k = opts.find('keep')
  return k == nil ? 0 : int(k)
end

def _http_send(method, url, body, cb, opts)
  var id = _http_next
  _http_next += 1
  if body == nil && opts != nil
    body = opts.find('body')
  end
  if body == nil body = "" end
  if _native_http_request(id, method, url, str(body), _http_headers(opts),
                          _http_find(opts), _http_keep(opts))
    _http_cbs[id] = [_native_app(), cb]
  else
    # No transport, a rejected request (bad method, oversized body, malformed
    # headers) or a full platform queue. Soft-fail exactly the way a network
    # error does, but immediately: the script needs no capability check.
    cb(nil, 0)
  end
end

# `opts` is nil-filled when the caller leaves it off, which Berry has no syntax
# for. The trailing comment is what the editor's signature table reads -- see
# scripts/berry_api.py -- and it only looks at a def whose header ends the line.
def _http_get(url, cb, opts) # http.get(url, cb, opts?)
  _http_send("GET", url, nil, cb, opts)
end
def _http_delete(url, cb, opts) # http.delete(url, cb, opts?)
  _http_send("DELETE", url, nil, cb, opts)
end
def _http_post(url, body, cb, opts) # http.post(url, body, cb, opts?)
  _http_send("POST", url, body, cb, opts)
end
def _http_put(url, body, cb, opts) # http.put(url, body, cb, opts?)
  _http_send("PUT", url, body, cb, opts)
end
def _http_patch(url, body, cb, opts) # http.patch(url, body, cb, opts?)
  _http_send("PATCH", url, body, cb, opts)
end
def _http_request(method, url, cb, opts) # http.request(method, url, cb, opts?)
  _http_send(method, url, nil, cb, opts)
end

http.get = _http_get
http.delete = _http_delete
http.post = _http_post
http.put = _http_put
http.patch = _http_patch
http.request = _http_request

# status 0 with body nil signals "no response at all". Any real response reaches
# the callback, 4xx and 5xx included -- that is where an API explains itself.
# Called from C, never from script code. The host has already set the current app
# to the request's owner, but the callback is a closure that captured its own
# `self`, so it runs correctly regardless.
def _dispatch_http(id, body, status)
  if _http_cbs.contains(id)
    var e = _http_cbs[id]
    _http_cbs.remove(id)   # remove BEFORE calling: a raising callback must not leak
    e[1](body, status)
  end
end

# BerryVM only offers string-argument calls, so C always dispatches through
# these two string-taking shims rather than through _dispatch_http directly.
def _dispatch_http_str(id, status, body) _dispatch_http(int(id), body, int(status)) end
def _dispatch_http_fail(id, status) _dispatch_http(int(id), nil, int(status)) end

# ---- mqtt ------------------------------------------------------------------
# Callbacks are keyed app -> {filter: cb}. The broker subscription itself is
# shared across apps by the host's adapter; this table is what routes a
# delivered message back to the apps that asked for it.
mqtt = module('mqtt')
_mqtt_cbs = {}

def _mqtt_publish(topic, payload)
  _native_mqtt_publish(topic, str(payload))
end

def _mqtt_subscribe(topic, cb)
  var app = _native_app()
  if !_mqtt_cbs.contains(app) _mqtt_cbs[app] = {} end
  var subs = _mqtt_cbs[app]
  # Cap per app (kMaxMqttSubs, spliced in from C -- see the top of this file).
  # Re-subscribing to a topic already held replaces its callback and does not
  # count again.
  if !subs.contains(topic) && size(subs) >= )BERRY" AWTRIX_PRELUDE_STR(
    AWTRIX_MAX_MQTT_SUBS) R"BERRY( return end
  subs[topic] = cb
  _native_mqtt_subscribe(topic)
end

mqtt.publish = _mqtt_publish
mqtt.subscribe = _mqtt_subscribe

# Two topics, on purpose. `filter` is what the app subscribed with and is the
# only thing the callback table can be keyed by -- it may contain wildcards, so
# the concrete topic would match nothing. `topic` is what the broker actually
# delivered on, and that is what the documented callback signature
# mqtt.subscribe(topic, def(topic, payload) end) hands the script: a subscriber
# to 'sensor/#' needs to know WHICH sensor spoke. The host sets the current app
# to the owner before calling this.
def _dispatch_mqtt(filter, topic, payload)
  var app = _native_app()
  if _mqtt_cbs.contains(app) && _mqtt_cbs[app].contains(filter)
    _mqtt_cbs[app][filter](topic, payload)
  end
end

# ---- numbers ---------------------------------------------------------------
# num(v) turns a value that ought to be a number into one, or returns the
# default (nil unless given). It exists because the obvious check does not
# work: Berry's isinstance() takes an instance and a class, but ints and reals
# are primitives and `int`/`real` are the baselib conversion FUNCTIONS -- so
# isinstance(876.6, real) is quietly false and a script that guards with it
# drops every value it receives. type() is the primitive-safe test, and this
# helper is the one place that has to know that.
#
# Accepted: ints and reals (returned as-is), strings holding a bare JSON
# number ("876.6"), and strings holding a JSON-quoted number ("\"876.6\"" --
# what a broker publishing string-typed states emits). Everything else --
# units ("876.6 W"), decimal commas ("876,6"), maps, bools, garbage -- yields
# the default. json.load is the parser on purpose: it is strict, and a strict
# nil beats number()'s silent 0-for-garbage and 876-for-"876,6".
# round/clamp/min/max are the arithmetic every second script hand-rolls, so
# they live here once. round() is half-away-from-zero -- what a human reading
# a sensor value expects -- via int()'s truncation toward zero, so it needs no
# math import. clamp/min/max compare with < only, so they work on anything
# Berry can order, strings included.
def round(v, digits) # round(v, digits?)
  var t = type(v)
  if t == 'int' return v end
  if t != 'real' return nil end
  if digits == nil || digits <= 0
    return v >= 0 ? int(v + 0.5) : int(v - 0.5)
  end
  var f = 1
  for i : 1 .. digits f *= 10 end
  var scaled = v >= 0 ? int(v * f + 0.5) : int(v * f - 0.5)
  return scaled / (f * 1.0)
end

def clamp(v, lo, hi) # clamp(v, lo, hi)
  if v < lo return lo end
  if v > hi return hi end
  return v
end

def min(a, b) # min(a, b)
  return a < b ? a : b
end

def max(a, b) # max(a, b)
  return a > b ? a : b
end

def num(v, dflt) # num(v, dflt?)
  var t = type(v)
  if t == 'int' || t == 'real' return v end
  if t == 'string'
    v = json.load(v)
    t = type(v)
    if t == 'int' || t == 'real' return v end
    if t == 'string'
      # One unwrap only: the payload was a JSON string whose content may
      # itself be a number. "\"876.6\"" passes; "\"\\\"876.6\\\"\"" does not.
      v = json.load(v)
      t = type(v)
      if t == 'int' || t == 'real' return v end
    end
  end
  return dflt
end

# ---- notify ----------------------------------------------------------------
# notify(spec) posts a notification. `spec` is a map in the notification-payload
# schema (text, icon, textColor, sound, soundRtttl, hold, stack, wakeup, effect, ...) --
# the same object a pushed app or POST /api/v1/notifications takes -- so a script
# reaches the whole notification pipeline, interruption and wakeup included, that
# its own canvas cannot. Colours may be plain 0xRRGGBB integers (rgb()/hsv() return
# those). Returns true when the device accepted it. Serialising here keeps the C
# side to one string primitive.
def notify(spec)
  return _native_notify(json.dump(spec))
end

# ---- settings --------------------------------------------------------------
# The device configuration, addressed by the same keys PATCH /api/v1/settings
# takes -- 'brightness', 'textColor', 'appDurationMs'. get() answers nil for an
# unknown key and for an accent colour the user never set; fall back to
# get('textColor'), which is what the built-in apps do. set() validates before
# it queues, so an unknown key, a wrong type or an out-of-range value returns
# false and changes nothing. Values are what the user CONFIGURED --
# get('brightness') is the setting, not what auto-brightness has the panel lit
# at this second.
settings = module('settings')
def _settings_get(k) # settings.get(key)
  return _native_settings_get(str(k))
end
def _settings_set(k, v) # settings.set(key, value)
  return _native_settings_set(str(k), v)
end
# The uppercase switch is applied by the renderer to a pushed app's text and
# never to what a script draws -- the canvas is yours. This hands the same
# transform back to a script that wants to match, run by the renderer's own
# code rather than a second copy that could drift from it.
def _settings_apply_case(s) # settings.apply_case(str)
  return _native_apply_case(str(s))
end
settings.get = _settings_get
settings.set = _settings_set
settings.apply_case = _settings_apply_case

# ---- sound -----------------------------------------------------------------
# Queued for the device to play, not played inside your draw call: the request
# takes the same route POST /api/v1/sounds/play does, so the "sound is switched
# off" and "this backend has no RTTTL" rules are the device's, decided once.
# True means the request was accepted, not that a file of that name exists.
# The action numbers are script::SoundAction (Play, Rtttl, Stop) in that order.
sound = module('sound')
def _sound_play(name) # sound.play(name)
  return _native_sound(0, str(name))
end
def _sound_rtttl(melody) # sound.rtttl(melody)
  return _native_sound(1, str(melody))
end
def _sound_stop() # sound.stop()
  return _native_sound(2, '')
end
sound.play = _sound_play
sound.rtttl = _sound_rtttl
sound.stop = _sound_stop

# ---- sensor ----------------------------------------------------------------
# What the device measures, straight from the reading the built-in apps draw.
# Every call answers nil when the board has no such sensor, so a panel without a
# BME280 reads nil rather than a convincing 0 degrees -- check before you draw.
# Temperature is ALWAYS Celsius and humidity always a percentage: the raw value
# stays raw, and settings.get('useCelsius') tells you what the user wants to see.
# Values refresh on the device's own schedule, not per frame.
sensor = module('sensor')
def _sensor_temperature() # sensor.temperature()
  return _native_temperature()
end
def _sensor_humidity() # sensor.humidity()
  return _native_humidity()
end
def _sensor_pressure() # sensor.pressure()
  return _native_pressure()
end
def _sensor_light() # sensor.light()
  return _native_light()
end
def _sensor_battery() # sensor.battery()
  return _native_battery()
end
def _sensor_battery_volts() # sensor.battery_volts()
  return _native_battery_volts()
end
sensor.temperature = _sensor_temperature
sensor.humidity = _sensor_humidity
sensor.pressure = _sensor_pressure
sensor.light = _sensor_light
sensor.battery = _sensor_battery
sensor.battery_volts = _sensor_battery_volts

# ---- rotation --------------------------------------------------------------
# Drive the app rotation the script lives in. rotation.next()/previous() advance
# now and keep any pause you set (so a paused app can step itself), while
# rotation.pause()/resume() hold and release the auto-advance clock. A paused
# rotation stays paused until rotation.resume() -- or until the user moves the
# display by button or API, which always returns to normal rotation.
# rotation.show() brings the rotation to THIS app now, so a script with something
# to say does not have to wait for its turn. It can only summon itself: the
# caller's name comes from the binding, not from an argument. Any pause you set
# survives it. Returns false when the app is not in the rotation at all.
rotation = module('rotation')
def _rotation_next() # rotation.next()
  _native_rotation_next()
end
def _rotation_previous() # rotation.previous()
  _native_rotation_prev()
end
def _rotation_pause() # rotation.pause()
  _native_rotation_hold(true)
end
def _rotation_resume() # rotation.resume()
  _native_rotation_hold(false)
end
def _rotation_show() # rotation.show()
  return _native_rotation_show()
end
rotation.next = _rotation_next
rotation.previous = _rotation_previous
rotation.pause = _rotation_pause
rotation.resume = _rotation_resume
rotation.show = _rotation_show

# ---- store -----------------------------------------------------------------
# Per-app whole-map write-behind: every set() re-serialises the calling app's
# store and hands it to the host, which decides when it actually reaches flash.
store = module('store')
_stores = {}

def _store_map()
  var app = _native_app()
  if !_stores.contains(app) _stores[app] = {} end
  return _stores[app]
end

def _store_set(k, v)
  var m = _store_map()
  m[k] = v
  _native_store_flush(json.dump(m))
end

# The contract documents store.get(key, default?) with the default OPTIONAL, and
# a plain 2-parameter function is all that takes: Berry nil-fills the arguments a
# caller left off (prep_closure in be_vm.c), so a 1-argument call arrives with
# dflt already nil -- which is exactly the documented "nil if never written".
def _store_get(k, dflt) # store.get(k, dflt?)
  var m = _store_map()
  if m.contains(k) return m[k] end
  return dflt
end

store.set = _store_set
store.get = _store_get

# Seeds the persisted store for the current app. Called from C once per app,
# under a BindingScope set to that app, after the app is loaded, so its setup()
# already sees restored values.
#
# The isinstance check is load-bearing, not defensive noise. json.load('[1,2]')
# succeeds and returns a LIST, which would make the store a list and raise on
# the very next store.get -- turning "the stored blob is corrupt" into "the
# script is bricked", which is precisely what this path promises cannot happen.
# Only a map is a store; anything else means start empty.
def _store_load(j)
  var m = json.load(j)
  if isinstance(m, map) _stores[_native_app()] = m end
end

# ---- shared ----------------------------------------------------------------
# The volatile space apps exchange values through. Writes take a BARE key and
# are filed under the calling app; reads take "owner.key" (a bare name reads
# one's own). Values are scalars -- int, real, bool, string -- because in one
# shared VM a map handed across would stay a single object the reader could
# mutate. Publish json.dump(...) if structure is needed.
#
# The whole module is a thin forward to C: the namespace prefixing, the caps and
# the timestamps all live there, where the calling app is already known and
# cannot be spoofed. Nothing here survives a reboot -- that is what `store` is
# for; a provider republishes from setup().
shared = module('shared')

# Returns false when the key is malformed (empty, over 24 chars, or holding
# anything outside A-Z a-z 0-9 _ -), when the value is not a scalar, or when the
# app is at its 8-key / 256-byte budget. A refused write changes nothing.
# Passing nil as the value erases the key.
def _shared_set(k, v) # shared.set(key, value)
  return _native_shared_set(k, v)
end

def _shared_get(k, dflt) # shared.get(key, dflt?)
  var v = _native_shared_get(k)
  if v == nil return dflt end
  return v
end

# Milliseconds since that value was last written, nil if there is no such key.
# This is how a reader notices a provider has stopped updating: the value is
# still there, but it stopped being true a while ago.
def _shared_age(k) # shared.age(key)
  return _native_shared_age(k)
end

# Every published key as "owner.key", or only one app's when given a name.
def _shared_keys(owner) # shared.keys(owner?)
  return _native_shared_keys(owner)
end

shared.set = _shared_set
shared.get = _shared_get
shared.age = _shared_age
shared.keys = _shared_keys

# ---- re --------------------------------------------------------------------
# Regular expressions over the firmware's own Pike-VM engine: linear time in
# the input, so no pattern can stall the panel. Byte-based, capturing groups,
# no {n,m} / backreferences / lookaround. Like http and store, `re` is simply
# there -- no import.
#
# The native call answers nil or [endOffset, group0, group1, ...]; endOffset
# exists for matchall's advance and is stripped before a script sees the list.
re = module('re')

# First match anywhere: nil, or a list whose [0] is the whole match and [1..]
# the capturing groups (nil for a group that did not take part).
def _re_search(pat, s) # re.search(pattern, text)
  var m = _native_re_search(pat, s, 0, 0)
  if m == nil return nil end
  m.remove(0)
  return m
end

# Same, but the match must start at the first byte.
def _re_match(pat, s) # re.match(pattern, text)
  var m = _native_re_search(pat, s, 0, 1)
  if m == nil return nil end
  m.remove(0)
  return m
end

# Every non-overlapping match, left to right. Full matches only -- reach for
# search() when the groups of one occurrence are wanted. An empty match steps
# one byte forward, so `a*` over "bb" terminates.
def _re_matchall(pat, s) # re.matchall(pattern, text)
  var out = []
  var from = 0
  while from <= size(s)
    var m = _native_re_search(pat, s, from, 0)
    if m == nil break end
    out.push(m[1])
    if m[0] > from
      from = m[0] + 0
    else
      from = from + 1
    end
  end
  return out
end

re.search = _re_search
re.match = _re_match
re.matchall = _re_matchall

# ---- app registry ----------------------------------------------------------
_apps = {}
def _app_anchor(name, inst) _apps[name] = inst end
def _app_instance(name)
  if _apps.contains(name) return _apps[name] end
  return nil
end
def _app_drop(name)
  if _apps.contains(name) _apps.remove(name) end
end

# ---- teardown --------------------------------------------------------------
# Releases every prelude-owned trace of an app: its store, its mqtt callbacks,
# and any http callbacks still in flight. The app instance itself is dropped
# separately by the host (BerryVM::dropApp). Takes the name explicitly rather
# than via _native_app(), because teardown does not run inside the app.
def _app_forget(name)
  if _stores.contains(name) _stores.remove(name) end
  if _mqtt_cbs.contains(name) _mqtt_cbs.remove(name) end
  var dead = []
  for id : _http_cbs.keys()
    if _http_cbs[id][0] == name dead.push(id) end
  end
  for id : dead _http_cbs.remove(id) end
end
)BERRY";

#undef AWTRIX_PRELUDE_STR
#undef AWTRIX_PRELUDE_STR_

}
