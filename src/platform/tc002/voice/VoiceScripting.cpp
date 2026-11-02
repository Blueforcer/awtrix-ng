#include "platform/tc002/voice/VoiceScripting.h"

#include <algorithm>
#include <string_view>
#include <utility>

#include "berry.h"
#include "core/script/BerryVM.h"

extern "C" void be_gc_collect(bvm* vm);

namespace awtrix::tc002::voice {
namespace {

constexpr const char* kSourceStart = R"BERRY(
import global
import string
var m = module('voice')
)BERRY";

constexpr const char* kSource = R"BERRY(
var recs = {}
var plays = {}
var calls = {}
var release = _voice_release
global.undef('_voice_release')

class voice_clip
  var id, ms, kind, app
  def init(id, ms, kind)
    self.id = id
    self.ms = ms
    self.kind = kind
    self.app = _native_app()
  end
  def deinit() release(self.app, self.id) end
end

var numbers = def (text)
  var out = []
  for part : string.split(text, ' ') out.push(int(part)) end
  return out
end

var get = def (opts, key, fallback) return opts != nil ? opts.find(key, fallback) : fallback end

var ms = def (opts, key, scale)
  var value = get(opts, key, nil)
  return type(value) == 'int' || type(value) == 'real' ? int(value * scale) : value
end

var fields = def (map)
  if map == nil return '' end
  if classname(map) != 'map' return nil end
  var out = []
  for key : map.keys()
    var name = str(key)
    var value = str(map[key])
    if string.find(name, '=') >= 0 || string.find(value, '\x1f') >= 0 return nil end
    out.push(name + '=' + value)
  end
  return out.concat('\x1f')
end

var payload = def (data)
  if isinstance(data, voice_clip) return data.id end
  return type(data) == 'string' ? data : nil
end

var drop = def (entries, app)
  var gone = []
  for number : entries.keys() if entries[number][0] == app gone.push(number) end end
  for number : gone entries.remove(number) end
end

var take = def (entries, number)
  var entry = entries.find(number)
  if entry != nil entries.remove(number) end
  return entry
end

var started = def (entries, number, cb)
  if number < 0
    cb(nil, number)
  else
    entries[number] = [_native_app(), cb]
  end
end

global._voice_dispatch = def (kind, head, text)
  var n = numbers(head)
  if kind == 'record' || kind == 'fetch'
    var entries = kind == 'record' ? recs : calls
    var entry = take(entries, n[0])
    var clip = n[1] == 200 ? voice_clip(n[2], n[3], text) : nil
    if entry != nil entry[1](clip, n[1]) end
  elif kind == 'send'
    var entry = take(calls, n[0])
    var body = n[2] == 1 ? text : nil
    if entry != nil entry[1](body, n[1]) end
  elif kind == 'play'
    var entry = take(plays, n[0])
    if entry != nil && entry[1] != nil entry[1](n[1] == 1) end
  end
end
global._voice_drop = def (app, unused)
  drop(recs, app)
  drop(plays, app)
  drop(calls, app)
end

m.record = def (opts, cb)
  if type(cb) != 'function' raise 'value_error', 'voice.record needs a callback' end
  if opts != nil && classname(opts) != 'map' raise 'value_error', 'voice.record options must be a map' end
  started(recs, _voice_record(ms(opts, 'silence', 1), ms(opts, 'wait', 1000), ms(opts, 'max', 1000),
                              get(opts, 'from', nil)), cb)
end
m.stop = def () _voice_stop() end
m.cancel = def () _voice_cancel() end
m.level = def () return _voice_level() end
m.state = def () return _voice_state() end
m.play = def (clip, done)
  if !isinstance(clip, voice_clip) raise 'value_error', 'voice.play needs a clip' end
  var number = _voice_play(clip.id)
  if number == 0 return false end
  plays[number] = [_native_app(), done, clip]
  return true
end
m.send = def (data, url, opts, cb)
  if type(cb) != 'function' raise 'value_error', 'voice.send needs a callback' end
  if opts != nil && classname(opts) != 'map' raise 'value_error', 'voice.send options must be a map' end
  var form = isinstance(data, voice_clip) ? opts : nil
  started(calls, _voice_send(payload(data), str(url), str(get(opts, 'method', 'POST')), _http_headers(opts),
                             str(get(form, 'as', 'form')), str(get(form, 'field', 'file')),
                             fields(get(form, 'fields', nil)), _http_find(opts), _http_keep(opts),
                             _http_cap(opts), get(opts, 'timeout', nil)), cb)
end
m.fetch = def (url, opts, cb)
  if type(cb) != 'function' raise 'value_error', 'voice.fetch needs a callback' end
  if opts != nil && classname(opts) != 'map' raise 'value_error', 'voice.fetch options must be a map' end
  var body = get(opts, 'body', nil)
  var method = get(opts, 'method', body != nil ? 'POST' : 'GET')
  var text = body != nil ? str(body) : ''
  started(calls, _voice_fetch(str(url), str(method), text, _http_headers(opts), get(opts, 'timeout', nil)), cb)
end
return m
)BERRY";

struct StatusName {
  const char* name;
  int status;
};
constexpr StatusName kStatusNames[] = {{"SILENCE", kSilence}, {"BUSY", kBusy}, {"CANCELLED", kCancelled},
                                       {"FULL", kFull},       {"GONE", kGone}, {"BAD", kBad}};

std::string moduleSource() {
  std::string source = kSourceStart;
  for (const StatusName& entry : kStatusNames)
    source += std::string("m.") + entry.name + " = " + std::to_string(entry.status) + "\n";
  return source + kSource;
}

struct Range {
  bint low, high;
};
constexpr Range kSilenceMs{300, 3000}, kWaitMs{1000, 15000}, kMaxMs{1000, 30000}, kTimeoutSec{1, 120};
constexpr uint32_t kSendTimeoutSec = 30, kFetchTimeoutSec = 15;

std::optional<uint32_t> rangeOption(bvm* vm, int index, Range range, uint32_t fallback) {
  if (be_isnil(vm, index)) return fallback;
  if (!be_isint(vm, index)) return std::nullopt;
  const bint value = be_toint(vm, index);
  if (value < range.low || value > range.high) return std::nullopt;
  return static_cast<uint32_t>(value);
}

std::optional<bool> triggerOption(bvm* vm, int index) {
  if (be_isnil(vm, index)) return true;
  if (!be_isstring(vm, index)) return std::nullopt;
  const std::string_view from = be_tostring(vm, index);
  if (from == "trigger" || from == "now") return from == "trigger";
  return std::nullopt;
}

const char* eventName(VoiceEvent::Kind kind) {
  switch (kind) {
    case VoiceEvent::Kind::Record: return "record";
    case VoiceEvent::Kind::Play: return "play";
    case VoiceEvent::Kind::Send: return "send";
    case VoiceEvent::Kind::Fetch: return "fetch";
  }
  return "";
}

std::string eventNumbers(const VoiceEvent& event) {
  const std::string head = std::to_string(event.number) + " " + std::to_string(event.status);
  switch (event.kind) {
    case VoiceEvent::Kind::Play: return head;
    case VoiceEvent::Kind::Send: return head + (event.body ? " 1" : " 0");
    case VoiceEvent::Kind::Record:
    case VoiceEvent::Kind::Fetch: return head + " " + std::to_string(event.clip) + " " + std::to_string(event.ms);
  }
  return head;
}

VoiceScripting& self(bvm* vm) { return *static_cast<VoiceScripting*>(script::BerryVM::nativeSelf(vm)); }
const std::string& caller() { return script::ScriptExtensionHost::caller(); }
std::string bytes(bvm* vm, int index) {
  return {be_tostring(vm, index), static_cast<std::size_t>(be_strlen(vm, index))};
}
VoiceTransfers::SendData sendData(bvm* vm, int index) {
  if (be_isint(vm, index)) return static_cast<ClipId>(be_toint(vm, index));
  return bytes(vm, index);
}

template <class Attempt>
int withRoomOfDroppedClips(bvm* vm, Attempt attempt) {
  const int result = attempt();
  if (result != kFull) return result;
  be_gc_collect(vm);
  return attempt();
}

}

VoiceScripting::VoiceScripting(Services services)
    : services_(std::move(services)),
      clips_(services_.clipDirectory, MemoryRoom(services_.availableMemory, kKeepFreeBytes)),
      transfers_(clips_, services_.fillRequest),
      recorder_(clips_, {services_.microphone, services_.startFrom, services_.live, services_.speaker,
                         services_.assistantBusy, services_.visible, services_.clock, services_.voiceActivityModel,
                         [this] { return claimVoiceChannel(); },
                         [this](const std::string& app) { return takeMark(app); },
                         [this](VoiceEvent event) { queue(std::move(event)); }}) {}

void VoiceScripting::install(script::ScriptExtensionHost& host) {
  host.defineNative("_voice_record", &VoiceScripting::nRecord, this);
  host.defineNative("_voice_stop", &VoiceScripting::nStop, this);
  host.defineNative("_voice_cancel", &VoiceScripting::nCancel, this);
  host.defineNative("_voice_play", &VoiceScripting::nPlay, this);
  host.defineNative("_voice_release", &VoiceScripting::nRelease, this);
  host.defineNative("_voice_level", &VoiceScripting::nLevel, this);
  host.defineNative("_voice_state", &VoiceScripting::nState, this);
  host.defineNative("_voice_send", &VoiceScripting::nSend, this);
  host.defineNative("_voice_fetch", &VoiceScripting::nFetch, this);
  host.defineModule(Module::kName, moduleSource());
}

void VoiceScripting::queue(VoiceEvent event) { events_.push_back(std::move(event)); }

void VoiceScripting::cancelEvent(VoiceEvent& event) {
  if (event.kind == VoiceEvent::Kind::Play) return;
  clips_.release(event.app, event.clip);
  event.status = kCancelled;
  event.clip = 0;
  event.ms = 0;
  event.format.clear();
  event.body.reset();
}

void VoiceScripting::tick(script::ScriptExtensionHost& host, const RenderCtx* ctx) {
  posix::Hold musicDuringCallback;
  for (std::size_t due = std::min<std::size_t>(events_.size(), kPerTick); due > 0 && !events_.empty(); --due) {
    VoiceEvent event = std::move(events_.front());
    events_.pop_front();
    if (voiceChannel_ && !voiceChannelWanted()) {
      if (!musicDuringCallback) musicDuringCallback = services_.speaker->holdMusic();
      voiceChannel_.reset();
    }
    const std::string& text = event.body ? *event.body : event.format;
    if (!host.deliver(event.app, Module::kName, "_voice_dispatch", eventName(event.kind), eventNumbers(event), text,
                      ctx) &&
        event.clip)
      clips_.release(event.app, event.clip);
  }
  deliverTrigger(host, ctx);
  settleSpeaker();
}

void VoiceScripting::update(int64_t now) {
  recorder_.update(now);
  while (std::optional<VoiceEvent> done = transfers_.poll()) queue(std::move(*done));
}

MicrophoneMark VoiceScripting::takeMark(const std::string& app) {
  return trigger_ && trigger_->app == app ? std::exchange(trigger_->mark, {}) : MicrophoneMark{};
}

void VoiceScripting::cancel(const std::string& app) {
  for (VoiceEvent& event : events_)
    if (event.app == app) cancelEvent(event);
  recorder_.end(app);
  for (VoiceEvent& done : transfers_.cancel(app)) queue(std::move(done));
}

int VoiceScripting::play(const std::string& app, ClipId clip) {
  const int number = recorder_.play(app, clip);
  if (!number) releaseVoiceChannel();
  return number;
}

int VoiceScripting::send(const std::string& app, VoiceTransfers::SendData data,
                         const VoiceTransfers::Options& options) {
  const int number = transfers_.send(app, std::move(data), options);
  updateMusic();
  return number;
}

int VoiceScripting::fetch(const std::string& app, std::string body, const VoiceTransfers::Options& options) {
  const int number = transfers_.fetch(app, std::move(body), options);
  updateMusic();
  return number;
}

bool VoiceScripting::voiceChannelWanted() const {
  return recorder_.active() ||
         std::any_of(events_.begin(), events_.end(), [](const VoiceEvent& e) { return !e.transfer(); });
}

bool VoiceScripting::musicWanted() const {
  return transfers_.holdsAny(services_.visible) ||
         std::any_of(events_.begin(), events_.end(),
                     [this](const VoiceEvent& e) { return e.transfer() && services_.visible(e.app); });
}

bool VoiceScripting::claimVoiceChannel() {
  if (!voiceChannel_) voiceChannel_ = services_.speaker->claimVoiceChannel();
  return static_cast<bool>(voiceChannel_);
}

void VoiceScripting::releaseVoiceChannel() {
  if (!voiceChannelWanted()) voiceChannel_.reset();
}

void VoiceScripting::updateMusic() {
  if (!musicWanted()) music_.reset();
  else if (!music_) music_ = services_.speaker->holdMusic();
}

void VoiceScripting::settleSpeaker() {
  updateMusic();
  releaseVoiceChannel();
}

bool VoiceScripting::holds(const std::string& app) const {
  return recorder_.holds(app) || transfers_.holds(app) ||
         std::any_of(events_.begin(), events_.end(), [&](const VoiceEvent& event) { return event.app == app; });
}

void VoiceScripting::trigger(const std::string& app, const std::string& name, MicrophoneMark mark) {
  trigger_ = Trigger{app, name, mark};
}

void VoiceScripting::dropTrigger() { trigger_.reset(); }

void VoiceScripting::dropTriggerOf(const std::string& app) {
  if (trigger_ && trigger_->app == app) trigger_.reset();
}

void VoiceScripting::beginFrame(script::ScriptExtensionHost&, const std::string& app) {
  if (trigger_ && trigger_->app == app) trigger_->drawn = true;
}

void VoiceScripting::deliverTrigger(script::ScriptExtensionHost& host, const RenderCtx* ctx) {
  if (!triggered() || !trigger_->drawn) return;
  trigger_->delivered = true;
  const std::string app = trigger_->app, name = trigger_->name;
  host.deliver(app, kHook, name, ctx);
}

void VoiceScripting::hidden(script::ScriptExtensionHost&, const std::string& app) {
  recorder_.end(app);
  clips_.releaseReady(app);
  dropTriggerOf(app);
}

void VoiceScripting::forget(script::ScriptExtensionHost& host, const std::string& app) {
  cancel(app);
  clips_.forget(app);
  dropTriggerOf(app);
  events_.erase(
      std::remove_if(events_.begin(), events_.end(), [&](const VoiceEvent& event) { return event.app == app; }),
      events_.end());
  host.call("_voice_drop", app, "");
  settleSpeaker();
}

int VoiceScripting::nRecord(bvm* vm) {
  int status = kBad;
  if (be_top(vm) >= 4) {
    const wake::EndpointSettings defaults;
    const std::optional<uint32_t> silence = rangeOption(vm, 1, kSilenceMs, defaults.silenceMs),
                                  wait = rangeOption(vm, 2, kWaitMs, defaults.waitMs),
                                  max = rangeOption(vm, 3, kMaxMs, defaults.maxMs);
    const std::optional<bool> fromTrigger = triggerOption(vm, 4);
    if (silence && wait && max && fromTrigger)
      status = withRoomOfDroppedClips(vm, [&] {
        return self(vm).record(caller(), {*silence, *wait, *max}, *fromTrigger);
      });
  }
  be_pushint(vm, status);
  be_return(vm);
}

int VoiceScripting::nStop(bvm* vm) {
  self(vm).stop(caller());
  be_return_nil(vm);
}

int VoiceScripting::nCancel(bvm* vm) {
  self(vm).cancel(caller());
  be_return_nil(vm);
}

int VoiceScripting::nPlay(bvm* vm) {
  be_pushint(vm, be_isint(vm, 1) ? self(vm).play(caller(), static_cast<ClipId>(be_toint(vm, 1))) : 0);
  be_return(vm);
}

int VoiceScripting::nRelease(bvm* vm) {
  if (be_isstring(vm, 1) && be_isint(vm, 2))
    self(vm).release(be_tostring(vm, 1), static_cast<ClipId>(be_toint(vm, 2)));
  be_return_nil(vm);
}

int VoiceScripting::nLevel(bvm* vm) {
  be_pushint(vm, self(vm).level(caller()));
  be_return(vm);
}

int VoiceScripting::nState(bvm* vm) {
  be_pushstring(vm, self(vm).state(caller()));
  be_return(vm);
}

int VoiceScripting::nSend(bvm* vm) {
  int result = kBad;
  if (be_top(vm) >= 11 && (be_isint(vm, 1) || be_isstring(vm, 1)) && be_isstring(vm, 2) && be_isstring(vm, 3) &&
      be_isstring(vm, 4) && be_isstring(vm, 5) && be_isstring(vm, 6) && be_isstring(vm, 7) && be_isstring(vm, 8) &&
      be_isint(vm, 9) && be_isint(vm, 10)) {
    const std::optional<uint32_t> timeout = rangeOption(vm, 11, kTimeoutSec, kSendTimeoutSec);
    if (timeout) {
      VoiceTransfers::Options options;
      options.url = be_tostring(vm, 2);
      options.method = be_tostring(vm, 3);
      options.headers = be_tostring(vm, 4);
      options.as = be_tostring(vm, 5);
      options.field = be_tostring(vm, 6);
      options.fields = bytes(vm, 7);
      options.find = be_tostring(vm, 8);
      options.keep = static_cast<int>(be_toint(vm, 9));
      options.cap = static_cast<int>(be_toint(vm, 10));
      options.timeoutSec = *timeout;
      result = withRoomOfDroppedClips(vm, [&] { return self(vm).send(caller(), sendData(vm, 1), options); });
    }
  }
  be_pushint(vm, result);
  be_return(vm);
}

int VoiceScripting::nFetch(bvm* vm) {
  int result = kBad;
  if (be_top(vm) >= 5 && be_isstring(vm, 1) && be_isstring(vm, 2) && be_isstring(vm, 3) && be_isstring(vm, 4)) {
    const std::optional<uint32_t> timeout = rangeOption(vm, 5, kTimeoutSec, kFetchTimeoutSec);
    if (timeout) {
      VoiceTransfers::Options options;
      options.url = be_tostring(vm, 1);
      options.method = be_tostring(vm, 2);
      options.headers = be_tostring(vm, 4);
      options.timeoutSec = *timeout;
      result = withRoomOfDroppedClips(vm, [&] { return self(vm).fetch(caller(), bytes(vm, 3), options); });
    }
  }
  be_pushint(vm, result);
  be_return(vm);
}

}
