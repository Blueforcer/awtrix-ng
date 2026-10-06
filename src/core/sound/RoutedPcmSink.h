#pragma once

#include "core/sound/AudioSinks.h"
#include "core/sound/PcmRouting.h"

namespace awtrix::sound {

// An IPcmSink that answers through PcmRouting. A sink implements the primitives below that it
// has; every other one says it cannot.
class RoutedPcmSink : public IPcmSink {
 public:
  sound::PcmPlay playSpec(const sound::PcmRequest& request, DispatchDetail& detail) override {
    return routing.play(*this, request, detail);
  }
  sound::Caps caps() const override { return PcmRouting::caps(*this); }
  bool checkSpec(const sound::Spec& spec, DispatchDetail& detail) override {
    return PcmRouting::check(*this, spec, detail);
  }
  void stopLayers(sound::Stop what, const std::string& owner) override { routing.stop(*this, what, owner); }
  sound::PcmState state() override { return routing.state(*this); }
  bool takeError(sound::PcmError&) override { return false; }
  PcmRouting routing;
  virtual ~RoutedPcmSink() = default;
  virtual void setVolumes(const Volumes& volumes) = 0;

  // The caller hands in a validated path: "/MP3/<name>.mp3" or "/SCRIPTS/<script>/<name>.mp3".
  virtual bool playMp3(const std::string& path, Group group) = 0;
  virtual void stopOneShot() = 0;
  virtual bool oneShotPlaying() const = 0;
  // The group of the one-shot the sink plays now; false when it cannot say. It differs from the
  // router's when the sink replaced that one-shot by itself, as the TC002's voice does.
  virtual bool oneShotGroup(Group& group) const {
    (void)group;
    return false;
  }

  virtual bool mixes() const { return false; }
  virtual bool playEffect(const std::string& path) { return playMp3(path, Group::App); }
  virtual bool playLoop(const std::string& path) {
    (void)path;
    return false;
  }
  virtual void stopEffects() {}
  virtual void stopLoop() {}
  virtual bool effectsPlaying() const { return false; }
  // The loop's layer has music, also while it is still being fetched; false once the sink dropped
  // it by itself.
  virtual bool loopPlaying() const { return false; }

  // A sink that synthesizes plays song text: as app music on the loop layer (playSong), as an app
  // effect (playFx), or once as the one-shot of any group (playSongOnce). checkSong() is the parse
  // alone; the play calls expect text it accepted and answer false only while the speaker cannot
  // play. With nextBar the song playing now runs to its next bar line and hands over there.
  virtual bool synthesizes() const { return false; }
  virtual bool checkSong(const std::string& text, std::string& error) {
    (void)text;
    error = "no synthesizer";
    return false;
  }
  virtual bool playSong(const std::string& text, bool nextBar) {
    (void)text;
    (void)nextBar;
    return false;
  }
  virtual bool playFx(const std::string& text) {
    (void)text;
    return false;
  }
  virtual bool playSongOnce(const std::string& text, Group group) {
    (void)text;
    (void)group;
    return false;
  }
  // Clips are always alerts. playClip() judges the bytes and takes them: ValidationError names
  // what is wrong, Unavailable means the speaker cannot play now.
  virtual bool clips() const { return false; }
  virtual DispatchResult playClip(std::string&& bytes, std::string& error) {
    (void)bytes;
    (void)error;
    return DispatchResult::Unavailable;
  }

  // Speech takes plain text; only the sink that speaks judges whether it has words to speak.
  virtual bool speaks() const { return false; }
  virtual bool checkSpeech(const std::string& text, DispatchDetail& detail) {
    (void)text;
    (void)detail;
    return false;
  }
  virtual DispatchResult playSpeech(const std::string& text, Group group, DispatchDetail& detail) {
    (void)text;
    (void)group;
    (void)detail;
    return DispatchResult::Unavailable;
  }

  // An MP3 from an address: as the one-shot of group, or with music as the app's looping music.
  // takeUrlError() hands over why one did not play, once, and whether it was the music.
  virtual bool fetches() const { return false; }
  virtual bool playUrl(const std::string& url, Group group, bool music) {
    (void)url;
    (void)group;
    (void)music;
    return false;
  }
  virtual bool takeUrlError(std::string& error, bool& music) {
    (void)error;
    (void)music;
    return false;
  }

  // Stops every layer that plays from path - that file, or any file inside that folder - and
  // returns once none of them is open: they are about to be deleted or replaced, and flash refuses
  // both for an open file. Everything else keeps playing.
  virtual void release(const std::string& path) { (void)path; }

  virtual DispatchResult playStream(const std::string& url, const std::string& label,
                                    DispatchDetail& detail) = 0;
  virtual void stopStream() = 0;
  // While held, a station that gave way to a one-shot does not come back: the one-shot repeats
  // and would cut it off again at once.
  virtual void holdStream(bool held) { (void)held; }

  virtual void tick(int64_t nowMs) = 0;

  // The analysed frame that is audible at nowMs; false when nothing plays or the output cannot
  // analyse. Asking is what switches the analysis on.
  bool analysis(int64_t nowMs, audio::FrameStats& out) override {
    (void)nowMs;
    (void)out;
    return false;
  }

  virtual uint32_t underruns() const { return 0; }
  virtual uint32_t decodeUs() const { return 0; }
  virtual uint32_t starvedMs() const { return 0; }
  virtual uint32_t bufferBytes() const { return 0; }
};

}
