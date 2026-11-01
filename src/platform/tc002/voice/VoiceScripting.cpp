#include "platform/tc002/voice/VoiceScripting.h"

#include <algorithm>
#include <string_view>
#include <utility>

#include "berry.h"
#include "core/script/BerryVM.h"
#include "platform/posix/Sequence.h"
#include "system/Log.h"

namespace awtrix::tc002::voice {
namespace {

constexpr const char* kSource = R"BERRY(
import global
import string
var m = module('voice')
m.SILENCE = -1
m.BUSY = -2
m.CANCELLED = -3
m.FULL = -4
m.GONE = -5
m.BAD = -6
var recs = {}
var plays = {}
var calls = {}

class voice_clip
  var id, ms, kind, app
  def init(id, ms, kind)
    self.id = id
    self.ms = ms
    self.kind = kind
    self.app = _native_app()
  end
  def deinit() _voice_release(self.app, self.id) end
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
m.send = def (clip, url, opts, cb)
  if type(cb) != 'function' raise 'value_error', 'voice.send needs a callback' end
  if !isinstance(clip, voice_clip) raise 'value_error', 'voice.send needs a clip' end
  if opts != nil && classname(opts) != 'map' raise 'value_error', 'voice.send options must be a map' end
  started(calls, _voice_send(clip.id, str(url), str(get(opts, 'method', 'POST')), _http_headers(opts),
                             str(get(opts, 'as', 'form')), str(get(opts, 'field', 'file')),
                             fields(get(opts, 'fields', nil)), _http_find(opts), _http_keep(opts),
                             _http_cap(opts)), cb)
end
m.fetch = def (url, opts, cb)
  if type(cb) != 'function' raise 'value_error', 'voice.fetch needs a callback' end
  if opts != nil && classname(opts) != 'map' raise 'value_error', 'voice.fetch options must be a map' end
  var body = get(opts, 'body', nil)
  var method = get(opts, 'method', body != nil ? 'POST' : 'GET')
  var text = body != nil ? str(body) : ''
  started(calls, _voice_fetch(str(url), str(method), text, _http_headers(opts)), cb)
end
return m
)BERRY";

constexpr int64_t kQuietWaitMs = 1000;

struct MsRange {
  bint low, high;
};
constexpr MsRange kSilenceMs{300, 3000}, kWaitMs{1000, 15000}, kMaxMs{1000, 30000};

std::optional<uint32_t> msOption(bvm* vm, int index, MsRange range, uint32_t fallback) {
  if (be_isnil(vm, index)) return fallback;
  if (!be_isint(vm, index)) return std::nullopt;
  const bint ms = be_toint(vm, index);
  if (ms < range.low || ms > range.high) return std::nullopt;
  return static_cast<uint32_t>(ms);
}

std::optional<bool> triggerOption(bvm* vm, int index) {
  if (be_isnil(vm, index)) return true;
  if (!be_isstring(vm, index)) return std::nullopt;
  const std::string_view from = be_tostring(vm, index);
  if (from == "trigger" || from == "now") return from == "trigger";
  return std::nullopt;
}

VoiceScripting& self(bvm* vm) { return *static_cast<VoiceScripting*>(script::BerryVM::nativeSelf(vm)); }
const std::string& caller() { return script::ScriptExtensionHost::caller(); }
std::string bytes(bvm* vm, int index) {
  return {be_tostring(vm, index), static_cast<std::size_t>(be_strlen(vm, index))};
}

}

VoiceScripting::VoiceScripting(Services services)
    : services_(std::move(services)),
      clips_(services_.clipDirectory, MemoryRoom(services_.availableMemory, kKeepFreeBytes)),
      transfers_(clips_, MemoryRoom(services_.availableMemory, kKeepFreeBytes)),
      capture_(services_.microphone, services_.startFrom, services_.voiceActivityModel) {}

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
  host.defineModule(Module::kName, kSource);
}

VoiceScripting::Event VoiceScripting::outcome(std::string app, EventKind kind, int number, int status) {
  Event event{};
  event.app = std::move(app);
  event.kind = kind;
  event.number = number;
  event.status = status;
  return event;
}

VoiceScripting::Event VoiceScripting::delivery(std::string app, EventKind kind, int number, ClipId clip, uint32_t ms,
                                               std::string format) {
  Event event = outcome(std::move(app), kind, number, kOk);
  event.clip = clip;
  event.ms = ms;
  event.text = std::move(format);
  return event;
}

const char* VoiceScripting::eventName(EventKind kind) {
  switch (kind) {
    case EventKind::Record: return "record";
    case EventKind::Play: return "play";
    case EventKind::Send: return "send";
    case EventKind::Fetch: return "fetch";
  }
  return "";
}

std::string VoiceScripting::eventNumbers(const Event& event) {
  const std::string head = std::to_string(event.number) + " " + std::to_string(event.status);
  switch (event.kind) {
    case EventKind::Play: return head;
    case EventKind::Send: return head + (event.text ? " 1" : " 0");
    case EventKind::Record:
    case EventKind::Fetch: return head + " " + std::to_string(event.clip) + " " + std::to_string(event.ms);
  }
  return head;
}

bool VoiceScripting::transferEvent(const Event& event) {
  return event.kind == EventKind::Send || event.kind == EventKind::Fetch;
}

VoiceScripting::Event VoiceScripting::eventOf(VoiceTransfers::Done done) {
  const EventKind kind = done.kind == VoiceTransfers::Kind::Send ? EventKind::Send : EventKind::Fetch;
  Event event = outcome(std::move(done.app), kind, done.number, done.status);
  event.clip = done.clip;
  event.text = std::move(done.text);
  return event;
}

void VoiceScripting::queue(Event event) { events_.push_back(std::move(event)); }

void VoiceScripting::cancelEvent(Event& event) {
  if (event.kind == EventKind::Play) return;
  clips_.release(event.app, event.clip);
  event.status = kCancelled;
  event.clip = 0;
  event.ms = 0;
  event.text.reset();
}

void VoiceScripting::tick(script::ScriptExtensionHost& host, const RenderCtx* ctx) {
  for (std::size_t due = std::min<std::size_t>(events_.size(), kPerTick); due > 0 && !events_.empty(); --due) {
    Event event = std::move(events_.front());
    events_.pop_front();
    if (!host.deliver(event.app, Module::kName, "_voice_dispatch", eventName(event.kind), eventNumbers(event),
                      event.text.value_or(""), ctx) &&
        event.clip)
      clips_.release(event.app, event.clip);
  }
  deliverTrigger(host, ctx);
  settleSpeaker();
}

void VoiceScripting::update(int64_t now) {
  if (pendingCapture_ && (services_.speaker->quiet() || now - pendingCapture_->since >= kQuietWaitMs)) {
    const wake::EndpointSettings settings = pendingCapture_->settings;
    pendingCapture_.reset();
    startCapture(settings, {}, now);
  }
  capture_.tick(now);
  if (const auto result = capture_.take()) {
    if (result->outcome == VoiceCapture::Outcome::Failed)
      logf("voice: recording failed: %s", result->error.c_str());
    if (result->outcome == VoiceCapture::Outcome::Recorded &&
        clips_.commit(recordClip_, result->bytes, result->ms, ClipKind::Wav))
      deliverRecording(result->ms);
    else
      endRecording(result->outcome == VoiceCapture::Outcome::Silence ? kSilence : kBusy);
  }
  while (std::optional<VoiceTransfers::Done> done = transfers_.poll()) queue(eventOf(std::move(*done)));
  if (playing_ && services_.speaker->finished()) endPlayback(services_.speaker->heard());
}

int VoiceScripting::record(const std::string& app, const wake::EndpointSettings& settings, bool fromTrigger) {
  if (!services_.visible(app) || services_.assistantBusy() || !recordApp_.empty() || !clips_.ready()) return kBusy;
  std::string error;
  if (!capture_.prepare(error)) {
    logf("voice: recording failed: %s", error.c_str());
    return kBusy;
  }
  const ClipId clip = clips_.reserve(app, capture_.maxBytes(settings));
  if (!clip || !claimVoiceChannel()) {
    if (clip) clips_.release(app, clip);
    capture_.cancel();
    return clip ? kBusy : kFull;
  }
  const MicrophoneMark mark = takeMark(app);
  recordApp_ = app;
  recordClip_ = clip;
  recordNumber_ = posix::nextInSequence(recordNumber_);
  if (fromTrigger && services_.live(mark))
    startCapture(settings, mark, services_.clock());
  else
    pendingCapture_ = PendingCapture{settings, services_.clock()};
  return recordNumber_;
}

MicrophoneMark VoiceScripting::takeMark(const std::string& app) {
  return trigger_ && trigger_->app == app ? std::exchange(trigger_->mark, {}) : MicrophoneMark{};
}

void VoiceScripting::startCapture(const wake::EndpointSettings& settings, MicrophoneMark from, int64_t now) {
  if (!capture_.start(clips_.path(recordClip_), settings, from, now)) endRecording(kBusy);
}

void VoiceScripting::deliverRecording(uint32_t ms) {
  queue(delivery(recordApp_, EventKind::Record, recordNumber_, recordClip_, ms, clipFormat(ClipKind::Wav).extension));
  clearRecording();
}

void VoiceScripting::endRecording(int status) {
  clips_.release(recordApp_, recordClip_);
  queue(outcome(recordApp_, EventKind::Record, recordNumber_, status));
  clearRecording();
}

void VoiceScripting::clearRecording() {
  capture_.cancel();
  recordApp_.clear();
  recordClip_ = 0;
  pendingCapture_.reset();
}

void VoiceScripting::stop(const std::string& app) {
  if (recordApp_.empty() || recordApp_ != app) return;
  if (pendingCapture_)
    endRecording(kSilence);
  else
    capture_.stop();
}

void VoiceScripting::stopAudio(const std::string& app) {
  if (!recordApp_.empty() && recordApp_ == app) endRecording(kCancelled);
  if (playing_ && playApp_ == app) {
    services_.speaker->stop();
    endPlayback(false);
  }
}

void VoiceScripting::cancel(const std::string& app) {
  for (Event& event : events_)
    if (event.app == app) cancelEvent(event);
  stopAudio(app);
  for (VoiceTransfers::Done& done : transfers_.cancel(app)) queue(eventOf(std::move(done)));
}

int VoiceScripting::play(const std::string& app, ClipId id) {
  const Clip* clip = clips_.find(app, id);
  if (!clip || services_.assistantBusy() || !services_.speaker->available()) return 0;
  if (playing_) {
    services_.speaker->stop();
    endPlayback(false);
  }
  if (!claimVoiceChannel()) return 0;
  if (!services_.speaker->play(clip->path)) {
    releaseVoiceChannel();
    return 0;
  }
  playing_ = true;
  playApp_ = app;
  playNumber_ = posix::nextInSequence(playNumber_);
  return playNumber_;
}

void VoiceScripting::endPlayback(bool heard) {
  queue(outcome(playApp_, EventKind::Play, playNumber_, heard ? 1 : 0));
  playing_ = false;
  playApp_.clear();
}

int VoiceScripting::send(const std::string& app, ClipId clip, const VoiceTransfers::SendOptions& options) {
  const int number = transfers_.send(app, clip, options);
  updateMusic();
  return number;
}

int VoiceScripting::fetch(const std::string& app, const std::string& url, const std::string& method,
                          std::string body, const std::string& headers) {
  const int number = transfers_.fetch(app, url, method, std::move(body), headers);
  updateMusic();
  return number;
}

bool VoiceScripting::voiceChannelWanted() const {
  return !recordApp_.empty() || playing_ ||
         std::any_of(events_.begin(), events_.end(), [](const Event& e) { return !transferEvent(e); });
}

bool VoiceScripting::musicWanted() const {
  return !transfers_.empty() || std::any_of(events_.begin(), events_.end(), transferEvent);
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

const char* VoiceScripting::state(const std::string& app) const {
  if (!recordApp_.empty() && recordApp_ == app) return "recording";
  if (playing_ && playApp_ == app) return "playing";
  return "idle";
}

uint8_t VoiceScripting::level(const std::string& app) const {
  return !recordApp_.empty() && recordApp_ == app ? capture_.level() : 0;
}

bool VoiceScripting::holds(const std::string& app) const {
  return (!recordApp_.empty() && recordApp_ == app) || (playing_ && playApp_ == app) ||
         transfers_.holds(app) ||
         std::any_of(events_.begin(), events_.end(), [&](const Event& event) { return event.app == app; });
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
  host.deliver(app, "on_voice", name, ctx);
}

void VoiceScripting::hidden(script::ScriptExtensionHost&, const std::string& app) {
  stopAudio(app);
  clips_.releaseReady(app);
  dropTriggerOf(app);
}

void VoiceScripting::forget(script::ScriptExtensionHost& host, const std::string& app) {
  cancel(app);
  clips_.forget(app);
  dropTriggerOf(app);
  events_.erase(std::remove_if(events_.begin(), events_.end(), [&](const Event& event) { return event.app == app; }),
                events_.end());
  host.call("_voice_drop", app, "");
  settleSpeaker();
}

int VoiceScripting::nRecord(bvm* vm) {
  int status = kBad;
  if (be_top(vm) >= 4) {
    const wake::EndpointSettings defaults;
    const std::optional<uint32_t> silence = msOption(vm, 1, kSilenceMs, defaults.silenceMs),
                                  wait = msOption(vm, 2, kWaitMs, defaults.waitMs),
                                  max = msOption(vm, 3, kMaxMs, defaults.maxMs);
    const std::optional<bool> fromTrigger = triggerOption(vm, 4);
    if (silence && wait && max && fromTrigger)
      status = self(vm).record(caller(), {*silence, *wait, *max}, *fromTrigger);
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
  if (be_top(vm) >= 10 && be_isint(vm, 1) && be_isstring(vm, 2) && be_isstring(vm, 3) && be_isstring(vm, 4) &&
      be_isstring(vm, 5) && be_isstring(vm, 6) && be_isstring(vm, 7) && be_isstring(vm, 8) && be_isint(vm, 9) &&
      be_isint(vm, 10)) {
    VoiceTransfers::SendOptions options;
    options.url = be_tostring(vm, 2);
    options.method = be_tostring(vm, 3);
    options.headers = be_tostring(vm, 4);
    options.as = be_tostring(vm, 5);
    options.field = be_tostring(vm, 6);
    options.fields = bytes(vm, 7);
    options.find = be_tostring(vm, 8);
    options.keep = static_cast<int>(be_toint(vm, 9));
    options.cap = static_cast<int>(be_toint(vm, 10));
    result = self(vm).send(caller(), static_cast<ClipId>(be_toint(vm, 1)), options);
  }
  be_pushint(vm, result);
  be_return(vm);
}

int VoiceScripting::nFetch(bvm* vm) {
  int result = kBad;
  if (be_top(vm) >= 4 && be_isstring(vm, 1) && be_isstring(vm, 2) && be_isstring(vm, 3) && be_isstring(vm, 4))
    result = self(vm).fetch(caller(), be_tostring(vm, 1), be_tostring(vm, 2), bytes(vm, 3), be_tostring(vm, 4));
  be_pushint(vm, result);
  be_return(vm);
}

}
