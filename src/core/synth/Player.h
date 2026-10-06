#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "core/synth/Song.h"

namespace awtrix {
namespace synth {

// Plays one song at a time as mono float audio in -1..1. It allocates only when a song starts
// (voices hold a copy of their instrument, so the song they came from may go at any time) and
// never while it renders.
class Player {
 public:
  enum class Start : uint8_t { Now, NextBar };

  // voices is the polyphony shared by all tracks; a note beyond it takes over the quietest
  // releasing voice, else the oldest. An effect player plays each song once, whatever it says.
  Player(uint32_t rate, std::size_t voices, bool effect = false);
  ~Player();
  Player(const Player&) = delete;
  Player& operator=(const Player&) = delete;

  // Starts song from its top. NextBar lets the song already playing run to its next bar line and
  // hands over there, releasing its notes into the new one. The song playing now, asked again,
  // keeps playing.
  void play(std::shared_ptr<const Song> song, Start start = Start::Now);
  // Fades everything out within 10 ms.
  void stop();
  void render(float* out, std::size_t frames);
  // Nothing left to sound: no song, or a song that plays once has ended and its tails are gone.
  bool idle() const;
  const std::shared_ptr<const Song>& song() const { return song_; }
  // Beats since the top of the song at the render head; false without a song still playing.
  bool beat(double& out) const;

  static constexpr float kFadeMs = 10.0f;

 private:
  struct Voice;
  // Where a track is: its next note, and the voice and pitch it played last, for a slide.
  struct Cursor {
    std::size_t next = 0;
    int voice = -1;
    uint64_t order = 0;
    float pitch = -1.0f;
  };

  bool sequencing() const;
  void boundary();
  void begin(std::shared_ptr<const Song> song, double tick);
  void seek(double tick);
  void fire();
  void noteOn(std::size_t track, const Note& note);
  Voice& allocate();
  void releaseAll();
  double nextEventTick() const;
  void renderVoices(float* out, std::size_t frames);
  void configureEcho();

  const uint32_t rate_;
  const bool effect_;
  std::vector<Voice> voices_;
  std::vector<Cursor> cursors_;
  std::shared_ptr<const Song> song_;
  std::shared_ptr<const Song> pending_;
  double tick_ = 0.0;
  double samplesPerTick_ = 1.0;
  double switchTick_ = 0.0;
  bool ended_ = true;
  uint64_t order_ = 0;
  uint32_t noiseSeed_ = 0x9e3779b9u;
  // Fade-out after stop(): samples left and the gain step.
  uint32_t fadeLeft_ = 0;
  float fadeGain_ = 1.0f;
  float fadeStep_ = 0.0f;
  std::vector<float> echo_;
  std::size_t echoWrite_ = 0;
  std::size_t echoDelay_ = 0;
  float echoLow_ = 0.0f;
  float echoFeedback_ = 0.0f;
  float echoDamp_ = 1.0f;
  // The song playing now has an echo; without one, what an old echo still carries rings out and
  // nothing new goes in.
  bool echoOn_ = false;
  uint32_t echoTail_ = 0;
  std::vector<float> send_;
};

}
}
