#include "platform/tc002/audio/Tc002AudioSink.h"

#include <algorithm>

#include "system/MonotonicClock.h"

namespace awtrix {
namespace tc002 {

bool Tc002AudioSink::checkSong(const std::string& text, std::string& error) {
  const synth::ParseResult parsed = songs_.get(text);
  if (!parsed.ok()) error = parsed.describe();
  return parsed.ok();
}

bool Tc002AudioSink::playSong(const std::string& text, bool nextBar) {
  if (!available()) return false;
  const synth::ParseResult parsed = songs_.get(text);
  if (!parsed.ok()) return false;
  urls_.stop(true);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    ++request_.loopSeq;
    request_.loop.clear();
    request_.loop.song = parsed.song;
    request_.loop.nextBar = nextBar;
  }
  wake();
  return true;
}

bool Tc002AudioSink::playFx(const std::string& text) {
  if (!available() || voiceOwned_.load()) return false;
  const synth::ParseResult parsed = songs_.get(text);
  if (!parsed.ok()) return false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (request_.effects.size() >= kMaxQueuedEffects) return true;
    request_.effects.push_back({std::string(), parsed.song});
    ++effectsPending_;
  }
  wake();
  return true;
}

bool Tc002AudioSink::playSongOnce(const std::string& text, sound::Group group) {
  if (!available() || voiceOwned_.load()) return false;
  const synth::ParseResult parsed = songs_.get(text);
  if (!parsed.ok()) return false;
  urls_.stop(false);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    ++request_.oneShotSeq;
    request_.oneShot = Kind::Song;
    request_.oneShotGroup = group;
    request_.oneShotSong = parsed.song;
  }
  wake();
  return true;
}

// The newest position already heard, carried on to nowMs; the song loops back or is over at its
// end, and positions stop coming once no mixer plays.
bool Tc002AudioSink::songBeat(int64_t nowMs, double& beat) const {
  const uint32_t head = songMarks_.head();
  bool haveNewest = false, haveHeard = false;
  int64_t newestAt = 0, heardAt = 0;
  SongMark heard;
  for (uint32_t offset = 0; offset < kSongMarks; ++offset) {
    SongMark mark;
    int64_t at;
    if (!songMarks_.read(head - offset, at, mark) || !at) continue;
    if (!haveNewest || at > newestAt) { newestAt = at; haveNewest = true; }
    if (at <= nowMs && (!haveHeard || at > heardAt)) {
      heard = mark;
      heardAt = at;
      haveHeard = true;
    }
  }
  if (!haveNewest || newestAt < nowMs - kSongMarkStaleMs || !haveHeard || !heard.playing)
    return false;
  const MixSource::SongPosition& p = heard.position;
  double at = p.beat + static_cast<double>(nowMs - heardAt) * p.beatsPerMs;
  if (at >= p.endBeat) {
    if (!p.loops || p.endBeat <= p.loopBeat) return false;
    at = p.loopBeat + std::fmod(at - p.loopBeat, p.endBeat - p.loopBeat);
  }
  beat = at;
  return true;
}

// Audio thread, once per block the mixer hands out.
void Tc002AudioSink::markSong(int64_t audibleAtMs) {
  SongMark mark;
  mark.playing = mixer_ && mixer_->songPosition(mark.position);
  songMarks_.publish(mark, audibleAtMs);
}

void Tc002AudioSink::stopEffects() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    ++request_.effectsStopSeq;
    request_.effects.clear();
    effectsPending_ = 0;
  }
  wake();
}

void Tc002AudioSink::stopLoop() {
  urls_.stop(true);
  endLoopRequest();
}

void Tc002AudioSink::endLoopRequest() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (request_.loop.empty()) return;
    ++request_.loopSeq;
    request_.loop.clear();
  }
  wake();
}

bool Tc002AudioSink::effectsPlaying() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return effectsPending_ > 0 || effectVoices_.load() > 0;
}

// The loop's request is cleared wherever the sink drops it by itself: a released file, a mixer
// that ended or failed, the helper gone. The voice only holds it back.
bool Tc002AudioSink::loopPlaying() const {
  if (urls_.pending(true)) return true;
  std::lock_guard<std::mutex> lock(mutex_);
  return !request_.loop.empty();
}

}
}
