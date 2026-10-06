#include "platform/tc002/audio/Tc002AudioSink.h"

#include <algorithm>

#include "core/net/Url.h"
#include "core/sound/SoundMp3.h"
#include "platform/linux/host/HostStore.h"
#include "platform/tc002/speech/SpeechRequest.h"
#include "platform/tc002/speech/SpeechSource.h"
#include "system/Log.h"

namespace awtrix {
namespace tc002 {

namespace {
constexpr std::size_t kMaxMelodyFile = 4096;
}

bool Tc002AudioSink::playRtttl(const std::string& rtttl, sound::Group group) {
  if (!available() || voiceOwned_.load()) return false;
  rtttl::Parse parsed = rtttl::parse(rtttl);
  if (!parsed.ok || parsed.notes.empty()) return false;
  urls_.stop(false);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    ++request_.oneShotSeq;
    request_.oneShot = Kind::Tone;
    request_.oneShotGroup = group;
    request_.notes = std::move(parsed.notes);
    request_.timeUnit = parsed.timeUnit;
  }
  wake();
  return true;
}

bool Tc002AudioSink::playMelodyFile(const std::string& name, sound::Group group) {
  if (!available() || !rtttl::validName(name)) return false;
  const std::string path = host::hostPath("/MELODIES/" + name + ".txt");
  std::string content;
  if (path.empty() || !host::readFile(path, content, kMaxMelodyFile)) return false;
  return playRtttl(content, group);
}

bool Tc002AudioSink::playMp3(const std::string& path, sound::Group group) {
  if (!available() || voiceOwned_.load() || host::hostPath(path).empty()) return false;
  urls_.stop(false);
  return playOneShotMp3("", path, group);
}

bool Tc002AudioSink::playOneShotMp3(const std::string& file, const std::string& path, sound::Group group) {
  if (!available() || voiceOwned_.load()) return false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    ++request_.oneShotSeq;
    request_.oneShot = Kind::Mp3;
    request_.oneShotGroup = group;
    request_.mp3Path = path;
    request_.mp3File = file;
  }
  wake();
  return true;
}

// A playing melody ends before the fetch begins.
bool Tc002AudioSink::playUrl(const std::string& url, sound::Group group, bool music) {
  if (!available() || (!music && voiceOwned_.load())) return false;
  if (!music) {
    stop();
    std::lock_guard<std::mutex> lock(mutex_);
    urlGroup_ = group;
  }
  return urls_.play(url, music);
}

bool Tc002AudioSink::takeUrlError(std::string& error, bool& music) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (int loop = 0; loop < 2; ++loop) {
    if (!urlFailed_[loop]) continue;
    urlFailed_[loop] = false;
    error = std::move(urlError_[loop]);
    urlError_[loop].clear();
    music = loop != 0;
    return true;
  }
  return false;
}

UrlSounds::Speaker Tc002AudioSink::urlSpeaker() {
  UrlSounds::Speaker speaker;
  speaker.playOneShot = [this](const std::string& file, const std::string& url) {
    sound::Group group;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      group = urlGroup_;
    }
    return playOneShotMp3(file, url, group);
  };
  speaker.playLoop = [this](const std::string& file) { return loopFile(file); };
  speaker.stopOneShot = [this] { endOneShotRequest(); };
  speaker.stopLoop = [this] { endLoopRequest(); };
  speaker.seq = [this](bool loop) {
    std::lock_guard<std::mutex> lock(mutex_);
    return loop ? request_.loopSeq : request_.oneShotSeq;
  };
  speaker.playing = [this](bool loop) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (loop) return request_.loop.mp3;
    return request_.oneShot == Kind::Mp3 ? request_.mp3File : std::string();
  };
  speaker.failed = [this](const std::string& error, bool loop) {
    std::lock_guard<std::mutex> lock(mutex_);
    urlError_[loop] = error;
    urlFailed_[loop] = true;
  };
  return speaker;
}

DispatchResult Tc002AudioSink::playClip(std::string&& bytes, std::string& error) {
  if (!tc002::checkClip(bytes, error)) return DispatchResult::ValidationError;
  if (!available() || voiceOwned_.load()) return DispatchResult::Unavailable;
  auto clip = std::make_shared<const std::string>(std::move(bytes));
  {
    std::lock_guard<std::mutex> lock(mutex_);
    ++request_.oneShotSeq;
    request_.oneShot = Kind::Clip;
    request_.oneShotGroup = sound::Group::Alert;
    request_.clip = std::move(clip);
  }
  wake();
  return DispatchResult::Ok;
}

bool Tc002AudioSink::playEffect(const std::string& path) {
  const std::string file = host::hostPath(path);
  if (!available() || voiceOwned_.load() || file.empty()) return false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (request_.effects.size() >= kMaxQueuedEffects) return true;
    request_.effects.push_back({file, nullptr});
    ++effectsPending_;
  }
  wake();
  return true;
}

bool Tc002AudioSink::playLoop(const std::string& path) {
  const std::string file = host::hostPath(path);
  if (!available() || file.empty()) return false;
  urls_.stop(true);
  return loopFile(file);
}

bool Tc002AudioSink::loopFile(const std::string& file) {
  if (!available()) return false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    ++request_.loopSeq;
    request_.loop.clear();
    request_.loop.mp3 = file;
  }
  wake();
  return true;
}

bool Tc002AudioSink::checkSpeech(const std::string& text, DispatchDetail& detail) {
  auto plan = std::make_unique<speech::Plan>();
  return speech::readText(text, *plan, detail);
}

DispatchResult Tc002AudioSink::playSpeech(const std::string& text, sound::Group group, DispatchDetail& detail) {
  auto plan = std::make_shared<speech::Plan>();
  if (!speech::readText(text, *plan, detail)) return DispatchResult::ValidationError;
  if (!speaks() || voiceOwned_.load()) {
    detail.message = "speaker unavailable";
    return DispatchResult::Unavailable;
  }
  urls_.stop(false);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    ++request_.oneShotSeq;
    request_.oneShot = Kind::Speech;
    request_.oneShotGroup = group;
    request_.speech = std::move(plan);
  }
  wake();
  return DispatchResult::Ok;
}

bool Tc002AudioSink::playSystemSound(const std::string& path, uint32_t skipFrames) {
  if (!available() || path.empty()) return false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    ++request_.oneShotSeq;
    request_.oneShot = Kind::System;
    request_.oneShotGroup = sound::Group::Alert;
    request_.systemPath = path;
    request_.systemSkipFrames = skipFrames;
    systemSeq_.store(request_.oneShotSeq);
    systemAudibleAtMs_.store(-1);
    systemStart_.store(SystemStart::Pending);
    systemFinished_.store(false);
  }
  wake();
  return true;
}

// The voice ends the one-shot and the effects; the audio thread holds the station and the loop
// back until it lets go.
void Tc002AudioSink::setVoiceOwned(bool owned) {
  voiceQuiet_.store(false);
  voiceOwned_.store(owned);
  if (owned) {
    std::lock_guard<std::mutex> lock(mutex_);
    ++request_.oneShotSeq; request_.oneShot = Kind::None; request_.clip.reset(); request_.speech.reset();
    request_.oneShotSong.reset();
    ++request_.effectsStopSeq; request_.effects.clear(); effectsPending_ = 0;
  }
  wake();
}

void Tc002AudioSink::stopSystemSound() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (request_.oneShot != Kind::System) return;
    request_.oneShot = Kind::None;
    ++request_.oneShotSeq;
  }
  wake();
}

Tc002AudioSink::SystemStart Tc002AudioSink::systemStart(int64_t& audibleAtMs) const {
  const SystemStart start = systemStart_.load();
  audibleAtMs = start == SystemStart::Audible ? systemAudibleAtMs_.load() : -1;
  return start;
}

void Tc002AudioSink::stop() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (request_.oneShot != Kind::Tone) return;
    request_.oneShot = Kind::None;
    ++request_.oneShotSeq;
  }
  wake();
}

void Tc002AudioSink::stopOneShot() {
  urls_.stop(false);
  endOneShotRequest();
}

// A melody is the tone side's own; stop() ends it.
void Tc002AudioSink::endOneShotRequest() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (request_.oneShot == Kind::None || request_.oneShot == Kind::Tone) return;
    request_.oneShot = Kind::None;
    request_.clip.reset();
    request_.speech.reset();
    request_.oneShotSong.reset();
    ++request_.oneShotSeq;
  }
  wake();
}

// Stops playback from path without blocking; the audio thread drops matching effects.
void Tc002AudioSink::release(const std::string& path) {
  const std::string file = path.empty() ? std::string() : host::hostPath(path);
  if (!available() || file.empty()) return;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (request_.oneShot == Kind::Mp3 && sound::within(request_.mp3Path, path)) {
      request_.oneShot = Kind::None;
      ++request_.oneShotSeq;
    }
    std::vector<Effect>& queued = request_.effects;
    const std::size_t before = queued.size();
    queued.erase(std::remove_if(queued.begin(), queued.end(),
                                [&](const Effect& effect) {
                                  return !effect.mp3.empty() && sound::within(effect.mp3, file);
                                }),
                 queued.end());
    const uint32_t dropped = static_cast<uint32_t>(before - queued.size());
    effectsPending_ = effectsPending_ > dropped ? effectsPending_ - dropped : 0;
    if (!request_.loop.mp3.empty() && sound::within(request_.loop.mp3, file)) {
      ++request_.loopSeq;
      request_.loop.clear();
    }
    request_.released.push_back(file);
  }
  wake();
}

bool Tc002AudioSink::isPlaying() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return request_.oneShot == Kind::Tone;
}

// A melody is the tone side's (isPlaying). The boot sound and the voice assistant's answer count,
// so the router treats them as the alerts they are. A sound from an address counts while it is
// fetched.
bool Tc002AudioSink::oneShotPlaying() const {
  if (urls_.pending(false)) return true;
  std::lock_guard<std::mutex> lock(mutex_);
  return request_.oneShot != Kind::None && request_.oneShot != Kind::Tone;
}

bool Tc002AudioSink::oneShotGroup(sound::Group& group) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (request_.oneShot == Kind::None) return false;
  group = request_.oneShotGroup;
  return true;
}

DispatchResult Tc002AudioSink::playStream(const std::string& url, const std::string& label,
                                          DispatchDetail& detail) {
  (void)label;
  if (!net::parseUrl(url)) {
    detail.clear();
    detail.field = "url";
    detail.message = "invalid URL";
    return DispatchResult::ValidationError;
  }
  if (!available() || voiceOwned_.load()) {
    detail.clear();
    detail.message = "speaker unavailable";
    return DispatchResult::Unavailable;
  }
  routing_.clear();
  // A station asked for now ends the effects and the loop, which would otherwise hold the speaker
  // for good; a one-shot still finishes first.
  auto streamUrl = std::make_shared<const std::string>(url);
  urls_.stop(true);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    ++request_.streamSeq;
    report_.errorNew = report_.titleNew = false;
    request_.streamWanted = true;
    request_.url = std::move(streamUrl);
    ++request_.effectsStopSeq;
    request_.effects.clear();
    effectsPending_ = 0;
    if (!request_.loop.empty()) {
      ++request_.loopSeq;
      request_.loop.clear();
    }
  }
  wake();
  return DispatchResult::Ok;
}

void Tc002AudioSink::holdStream(bool held) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (request_.streamHeld == held) return;
    request_.streamHeld = held;
  }
  wake();
}

void Tc002AudioSink::stopStream() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!request_.streamWanted) return;
    request_.streamWanted = false;
    request_.url.reset();
    ++request_.streamSeq;
    report_.errorNew = report_.titleNew = false;
  }
  wake();
}

}
}
