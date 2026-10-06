#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <type_traits>
#include <vector>

namespace awtrix {
namespace synth {

// Where parsed songs keep their notes, tracks and instruments. Set once by the platform before the
// first song is parsed; a clock with PSRAM keeps songs out of its internal RAM.
struct SongMemory {
  void* (*allocate)(std::size_t) = std::malloc;
  void (*release)(void*) = std::free;
};
inline SongMemory& songMemory() {
  static SongMemory memory;
  return memory;
}
inline void setSongMemory(SongMemory memory) { songMemory() = memory; }

// Takes the memory set when it is made, so what it allocates goes back to the same release.
template <class T>
struct SongAllocator {
  using value_type = T;
  using propagate_on_container_copy_assignment = std::true_type;
  using propagate_on_container_move_assignment = std::true_type;
  using propagate_on_container_swap = std::true_type;

  SongMemory memory = songMemory();

  SongAllocator() = default;
  template <class U>
  SongAllocator(const SongAllocator<U>& other) : memory(other.memory) {}
  T* allocate(std::size_t n) {
    void* p = memory.allocate(n * sizeof(T));
    if (!p) std::abort();
    return static_cast<T*>(p);
  }
  void deallocate(T* p, std::size_t) { memory.release(p); }
  template <class U>
  bool operator==(const SongAllocator<U>& other) const {
    return memory.allocate == other.memory.allocate && memory.release == other.memory.release;
  }
  template <class U>
  bool operator!=(const SongAllocator<U>& other) const { return !(*this == other); }
};

template <class T>
using SongVector = std::vector<T, SongAllocator<T>>;

// A song counts time in ticks, 12 to the 16th note, so halves, thirds, quarters, sixths and
// twelfths of a 16th - triplets included - fall on whole ticks.
constexpr uint32_t kTicksPerSixteenth = 12;
constexpr uint32_t kTicksPerBeat = 4 * kTicksPerSixteenth;

enum class Wave : uint8_t { Pulse, Saw, Tri, Sine, Noise };
enum class Filter : uint8_t { None, LowPass, BandPass, HighPass };

// Everything a note of an instrument sounds like. Times are in ms, levels in 0..1. Plain data,
// so a voice copies it when it starts and never depends on the song after that.
struct Patch {
  Wave wave = Wave::Pulse;
  float duty = 0.5f;
  // Cents between two detuned oscillators; 0 plays one.
  float unison = 0.0f;
  // A triangle an octave below.
  float sub = 0.0f;
  float noise = 0.0f;
  // 0 lets the noise follow the envelope.
  float noiseDecayMs = 0.0f;
  float attackMs = 2.0f;
  float decayMs = 200.0f;
  float sustain = 0.7f;
  float releaseMs = 60.0f;
  // A note starts pitchSemis above its pitch and falls to it with this time constant.
  float pitchSemis = 0.0f;
  float pitchMs = 0.0f;
  float vibratoCents = 0.0f;
  float vibratoHz = 5.5f;
  float vibratoDelayMs = 0.0f;
  float glideMs = 60.0f;
  Filter filter = Filter::None;
  float cutoffHz = 20000.0f;
  float resonance = 0.0f;
  // The cutoff starts filterEnvHz higher and falls back with this time constant.
  float filterEnvHz = 0.0f;
  float filterEnvMs = 0.0f;
  float drive = 0.0f;
  float volume = 1.0f;
  float echo = 0.0f;
  // Share of its written length a note holds before it releases.
  float gate = 0.9f;
  // The pitch of a step in a % pattern, as a MIDI note.
  uint8_t stepPitch = 60;
};

struct Instrument {
  std::string name;
  Patch patch;
};

struct Note {
  uint32_t tick = 0;
  // Ticks the note is written for; the instrument's gate decides how much of it sounds.
  uint32_t length = 0;
  uint8_t pitch = 60;
  // 1..100.
  uint8_t velocity = 100;
  uint8_t instrument = 0;
  // Glides from the pitch the track played before instead of starting afresh.
  bool slide = false;
};

struct Track {
  std::string name;
  // In tick order; notes of one chord share a tick.
  SongVector<Note> notes;
};

struct Song {
  float bpm = 120.0f;
  uint8_t beatsPerBar = 4;
  // Whole bars: where a song that loops goes back.
  uint32_t lengthTicks = 0;
  // Where the longest track ends: a song that plays once, and every effect, is over there.
  uint32_t endTick = 0;
  bool loops = true;
  uint32_t loopTick = 0;
  float volume = 1.0f;
  // A delay line every instrument can send to; 0 sixteenths switches it off.
  float echoSixteenths = 0.0f;
  float echoFeedback = 0.0f;
  float echoDampHz = 4000.0f;
  SongVector<Instrument> instruments;
  SongVector<Track> tracks;

  uint32_t ticksPerBar() const { return beatsPerBar * kTicksPerBeat; }
  // Where the song as played goes back or ends.
  uint32_t endOf(bool once) const { return loops && !once ? lengthTicks : endTick; }
  double secondsPerTick() const { return 60.0 / (static_cast<double>(bpm) * kTicksPerBeat); }
  // What the song holds, for a cache that keeps a budget.
  std::size_t bytes() const;
};

}
}
