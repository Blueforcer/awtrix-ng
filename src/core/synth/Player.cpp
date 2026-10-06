#include "core/synth/Player.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace awtrix {
namespace synth {
namespace {

// Pitch, vibrato and filter follow at this many samples; the rest runs every sample.
constexpr std::size_t kControl = 32;
constexpr std::size_t kMaxBlock = 512;
constexpr float kVoiceGain = 0.25f;
constexpr double kEpsilon = 1e-6;
constexpr float kSilent = 1e-4f;
constexpr float kPi = 3.14159265358979f;
constexpr float kEdgeMargin = 1e-5f;
constexpr float kLn2PerSemitone = 0.05776226505f;

// phase in 0..1.
float sine(float phase) {
  float u = phase - 0.5f;
  if (u > 0.25f) u = 0.5f - u;
  else if (u < -0.25f) u = -0.5f - u;
  const float u2 = u * u;
  return u * (-6.283164f + u2 * (41.337143f + u2 * (-81.340836f + u2 * 70.99417f)));
}

// The polynomial band-limited step at t = 0; perDt is 1 / dt.
float blep(float t, float dt, float perDt) {
  if (t < dt) {
    t *= perDt;
    return t + t - t * t - 1.0f;
  }
  if (t > 1.0f - dt) {
    t = (t - 1.0f) * perDt;
    return t * t + t + t + 1.0f;
  }
  return 0.0f;
}

float wrap(float phase) { return phase >= 1.0f ? phase - 1.0f : phase; }

std::size_t within(float t, float low, float high, float perDt, std::size_t most) {
  if (t < low || t > high) return 0;
  const float room = (high - t) * perDt;
  return room >= static_cast<float>(most - 1) ? most : static_cast<std::size_t>(room) + 1;
}

float triangle(float t) { return 1.0f - 4.0f * std::fabs(t - 0.5f); }

// A cubic soft clip: small signals unchanged, full scale at |x| = 1.5.
float soft(float x) {
  if (x >= 1.5f) return 1.0f;
  if (x <= -1.5f) return -1.0f;
  return x - (4.0f / 27.0f) * x * x * x;
}

float white(uint32_t& state) {
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return static_cast<float>(static_cast<int32_t>(state)) * (1.0f / 2147483648.0f);
}

float perSample(float ms, uint32_t rate) {
  const float samples = ms * static_cast<float>(rate) / 1000.0f;
  return samples < 1.0f ? 0.0f : std::exp(-1.0f / samples);
}

float flush(float x) { return std::fabs(x) < 1e-15f ? 0.0f : x; }

}

struct Player::Voice {
  enum class Stage : uint8_t { Attack, Decay, Release };

  Patch patch;
  bool active = false;
  bool gated = false;
  std::size_t track = 0;
  uint64_t order = 0;
  uint32_t age = 0;
  uint32_t gateLeft = 0;
  float gain = 0.0f;
  // Pitch in MIDI notes: the target, where a glide set out from and what sounds now.
  float pitch = 60.0f;
  float glideFrom = 60.0f;
  float glideLeft = 0.0f;
  float glideTotal = 1.0f;
  float current = 60.0f;
  float pitchEnv = 0.0f;
  float pitchStep = 0.0f;
  float phase[2] = {0.0f, 0.5f};
  float inc[2] = {0.0f, 0.0f};
  float perInc[2] = {1.0f, 1.0f};
  float spread = 1.0f;
  float perSpread = 1.0f;
  float perRate = 0.0f;
  float depth = 0.0f;
  float subPhase = 0.0f;
  float subInc = 0.0f;
  Stage stage = Stage::Attack;
  float level = 0.0f;
  float attackStep = 1.0f;
  float decayCoef = 0.0f;
  float releaseFrom = 0.0f;
  float releaseU = 0.0f;
  float releaseStep = 1.0f;
  float releaseExp = 1.0f;
  float releaseExpStep = 1.0f;
  float noise = 0.0f;
  float noiseCoef = 1.0f;
  uint32_t rng = 1;
  float ic1 = 0.0f;
  float ic2 = 0.0f;
  float a1 = 0.0f;
  float a2 = 0.0f;
  float a3 = 0.0f;
  float k = 1.414f;
  float filterEnv = 0.0f;
  float filterStep = 0.0f;
  float driveGain = 1.0f;
  float driveNorm = 1.0f;
  float tunedPitch = 0.0f;
  float tunedCutoff = 0.0f;

  void start(const Patch& p, const Note& note, std::size_t t, uint64_t n, uint32_t gateSamples,
             uint32_t seed, uint32_t rate) {
    patch = p;
    active = true;
    gated = true;
    track = t;
    order = n;
    age = 0;
    gateLeft = gateSamples;
    gain = kVoiceGain * p.volume * static_cast<float>(note.velocity) / 100.0f;
    pitch = current = glideFrom = static_cast<float>(note.pitch);
    glideLeft = 0.0f;
    pitchEnv = p.pitchSemis;
    pitchStep = std::pow(perSample(p.pitchMs, rate), static_cast<float>(kControl));
    phase[0] = 0.0f;
    phase[1] = 0.5f;
    spread = std::exp2(p.unison / 2400.0f);
    perSpread = 1.0f / spread;
    perRate = 1.0f / static_cast<float>(rate);
    depth = p.vibratoCents / 100.0f;
    subPhase = 0.0f;
    stage = Stage::Attack;
    level = 0.0f;
    const float attack = p.attackMs * static_cast<float>(rate) / 1000.0f;
    attackStep = attack < 1.0f ? 1.0f : 1.0f / attack;
    decayCoef = perSample(p.decayMs, rate);
    noise = p.noise;
    noiseCoef = p.noiseDecayMs > 0.0f ? perSample(p.noiseDecayMs, rate) : 1.0f;
    rng = seed | 1u;
    ic1 = ic2 = 0.0f;
    k = 1.0f / (0.7071f + 11.0f * p.resonance);
    filterEnv = p.filterEnvHz;
    filterStep = std::pow(perSample(p.filterEnvMs, rate), static_cast<float>(kControl));
    driveGain = 1.0f + 9.0f * p.drive;
    driveNorm = 1.0f / soft(driveGain);
    tunedPitch = tunedCutoff = std::numeric_limits<float>::quiet_NaN();
  }

  __attribute__((noinline)) void release(uint32_t rate) {
    if (!active || stage == Stage::Release) return;
    gated = false;
    stage = Stage::Release;
    releaseFrom = level;
    const float samples = std::max(1.0f, patch.releaseMs * static_cast<float>(rate) / 1000.0f);
    releaseU = 0.0f;
    releaseStep = 1.0f / samples;
    releaseExp = 1.0f;
    releaseExpStep = std::exp(-5.0f / samples);
  }

  __attribute__((noinline)) void control(uint32_t rate) {
    const float fs = static_cast<float>(rate);
    float p = pitch;
    if (glideLeft > 0.0f) {
      const float u = glideLeft / glideTotal;
      p += (glideFrom - pitch) * u * u;
      glideLeft -= static_cast<float>(kControl);
    }
    p += pitchEnv;
    pitchEnv *= pitchStep;
    if (depth > 0.0f) {
      const float seconds = static_cast<float>(age) * perRate;
      float ramp = (seconds * 1000.0f - patch.vibratoDelayMs) * (1.0f / 200.0f);
      if (ramp < 0.0f) ramp = 0.0f;
      if (ramp > 1.0f) ramp = 1.0f;
      const float cycles = seconds * patch.vibratoHz;
      p += depth * ramp * sine(cycles - static_cast<float>(static_cast<uint32_t>(cycles)));
    }
    current = p;
    if (p != tunedPitch) {
      tunedPitch = p;
      const float hz = std::min(440.0f * std::exp((p - 69.0f) * kLn2PerSemitone), 0.45f * fs);
      const float step = hz * perRate;
      if (patch.unison > 0.0f) {
        inc[0] = step * perSpread;
        inc[1] = step * spread;
        perInc[1] = 1.0f / inc[1];
      } else {
        inc[0] = step;
      }
      perInc[0] = 1.0f / inc[0];
      subInc = 0.5f * step;
    }
    if (patch.filter != Filter::None) {
      const float cutoff = std::min(std::max(patch.cutoffHz + filterEnv, 20.0f), 0.45f * fs);
      filterEnv *= filterStep;
      if (cutoff != tunedCutoff) {
        tunedCutoff = cutoff;
        const float g = std::tan(kPi * cutoff * perRate);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
      }
      ic1 = flush(ic1);
      ic2 = flush(ic2);
    }
  }

  void oscillate(std::size_t n, float* y, std::size_t count) {
    float t = phase[n];
    const float dt = inc[n];
    const float perDt = perInc[n];
    switch (patch.wave) {
      case Wave::Pulse: {
        const float duty = patch.duty;
        const float offset = 2.0f * duty - 1.0f;
        const float edge = dt + kEdgeMargin;
        for (std::size_t i = 0; i < count;) {
          float level = 1.0f - offset;
          std::size_t run = within(t, edge, duty - edge, perDt, count - i);
          if (!run) {
            level = -1.0f - offset;
            run = within(t, duty + edge, 1.0f - edge, perDt, count - i);
          }
          if (run) {
            for (const std::size_t end = i + run; i < end; ++i) {
              y[i] = level;
              t += dt;
            }
            continue;
          }
          const bool high = t < duty;
          const float fall = t + 1.0f - duty;
          y[i++] = (high ? 1.0f : -1.0f) + blep(t, dt, perDt) -
                   blep(high ? fall : fall - 1.0f, dt, perDt) - offset;
          t = wrap(t + dt);
        }
        break;
      }
      case Wave::Saw: {
        const float edge = dt + kEdgeMargin;
        for (std::size_t i = 0; i < count;) {
          const std::size_t run = within(t, edge, 1.0f - edge, perDt, count - i);
          if (run) {
            for (const std::size_t end = i + run; i < end; ++i) {
              y[i] = 2.0f * t - 1.0f;
              t += dt;
            }
            continue;
          }
          y[i++] = 2.0f * t - 1.0f - blep(t, dt, perDt);
          t = wrap(t + dt);
        }
        break;
      }
      case Wave::Tri:
        for (std::size_t i = 0; i < count; ++i) {
          y[i] = triangle(t);
          t = wrap(t + dt);
        }
        break;
      default:
        for (std::size_t i = 0; i < count; ++i) {
          y[i] = sine(t);
          t = wrap(t + dt);
        }
        break;
    }
    phase[n] = t;
  }

  template <Filter Mode>
  void filter(float* y, std::size_t count) {
    float s1 = ic1;
    float s2 = ic2;
    const float c1 = a1;
    const float c2 = a2;
    const float c3 = a3;
    const float damping = k;
    for (std::size_t i = 0; i < count; ++i) {
      const float x = y[i];
      const float v3 = x - s2;
      const float v1 = c1 * s1 + c2 * v3;
      const float v2 = s2 + c2 * s1 + c3 * v3;
      s1 = 2.0f * v1 - s1;
      s2 = 2.0f * v2 - s2;
      y[i] = Mode == Filter::LowPass    ? v2
             : Mode == Filter::BandPass ? damping * v1
                                        : x - damping * v1 - v2;
    }
    ic1 = s1;
    ic2 = s2;
  }

  std::size_t envelope(float* env, std::size_t count, uint32_t rate) {
    std::size_t i = 0;
    while (i < count) {
      const std::size_t from = i;
      const std::size_t end = gated && gateLeft < count - i ? i + gateLeft : count;
      float lvl = level;
      switch (stage) {
        case Stage::Attack:
          while (i < end) {
            lvl += attackStep;
            if (lvl >= 1.0f) {
              lvl = 1.0f;
              stage = Stage::Decay;
              env[i++] = lvl;
              break;
            }
            env[i++] = lvl;
          }
          break;
        case Stage::Decay: {
          const float sustain = patch.sustain;
          const float coef = decayCoef;
          if (sustain <= 0.0f) {
            for (; i < end; ++i) {
              lvl = sustain + (lvl - sustain) * coef;
              if (lvl < kSilent) {
                level = lvl;
                active = false;
                return i;
              }
              env[i] = lvl;
            }
          } else {
            for (; i < end; ++i) {
              lvl = sustain + (lvl - sustain) * coef;
              env[i] = lvl;
            }
          }
          break;
        }
        case Stage::Release: {
          float u = releaseU;
          float e = releaseExp;
          for (; i < end; ++i) {
            u += releaseStep;
            e *= releaseExpStep;
            if (u >= 1.0f) {
              level = lvl;
              active = false;
              return i;
            }
            lvl = releaseFrom * e * (1.0f - u);
            env[i] = lvl;
          }
          releaseU = u;
          releaseExp = e;
          break;
        }
      }
      level = lvl;
      if (gated) {
        gateLeft -= static_cast<uint32_t>(i - from);
        if (gateLeft == 0) release(rate);
      }
    }
    return count;
  }

  // Adds up to frames samples into out and send; false once the voice has gone quiet.
  bool render(float* out, float* send, std::size_t frames, uint32_t rate) {
    float y[kControl];
    float more[kControl];
    std::size_t done = 0;
    while (done < frames) {
      const std::size_t into = age % kControl;
      if (into == 0) control(rate);
      const std::size_t n = std::min(frames - done, kControl - into);
      age += static_cast<uint32_t>(n);
      const bool noiseWave = patch.wave == Wave::Noise;
      if (noiseWave) {
        for (std::size_t i = 0; i < n; ++i) {
          y[i] = white(rng);
          more[i] = 0.0f;
          if (noise > 0.0f) {
            more[i] = noise * white(rng);
            noise *= noiseCoef;
          }
        }
      } else {
        oscillate(0, y, n);
        if (patch.unison > 0.0f) {
          oscillate(1, more, n);
          for (std::size_t i = 0; i < n; ++i) y[i] = 0.7071f * (y[i] + more[i]);
        }
      }
      if (patch.sub > 0.0f) {
        for (std::size_t i = 0; i < n; ++i) {
          y[i] += patch.sub * triangle(subPhase);
          subPhase = wrap(subPhase + subInc);
        }
      }
      if (noiseWave) {
        if (patch.noise > 0.0f)
          for (std::size_t i = 0; i < n; ++i) y[i] += more[i];
      } else if (noise > 0.0f) {
        for (std::size_t i = 0; i < n; ++i) {
          y[i] += noise * white(rng);
          noise *= noiseCoef;
        }
      }
      switch (patch.filter) {
        case Filter::None:
          break;
        case Filter::LowPass:
          filter<Filter::LowPass>(y, n);
          break;
        case Filter::BandPass:
          filter<Filter::BandPass>(y, n);
          break;
        case Filter::HighPass:
          filter<Filter::HighPass>(y, n);
          break;
      }
      if (patch.drive > 0.0f)
        for (std::size_t i = 0; i < n; ++i) y[i] = soft(y[i] * driveGain) * driveNorm;
      float* amp = more;
      const std::size_t sounding = envelope(amp, n, rate);
      const float scale = gain;
      const float echo = patch.echo;
      float* to = out + done;
      if (echo > 0.0f) {
        float* echoed = send + done;
        for (std::size_t i = 0; i < sounding; ++i) {
          const float v = y[i] * amp[i] * scale;
          to[i] += v;
          echoed[i] += v * echo;
        }
      } else {
        for (std::size_t i = 0; i < sounding; ++i) to[i] += y[i] * amp[i] * scale;
      }
      if (!active) return false;
      done += n;
    }
    return true;
  }
};

Player::Player(uint32_t rate, std::size_t voices, bool effect)
    : rate_(rate), effect_(effect), voices_(std::max<std::size_t>(1, voices)), send_(kMaxBlock) {}

Player::~Player() = default;

void Player::play(std::shared_ptr<const Song> song, Start start) {
  if (!song) return;
  if (fadeLeft_) {
    for (Voice& v : voices_) v.active = false;
    fadeLeft_ = 0;
    fadeGain_ = 1.0f;
  }
  if (song_ == song && !ended_) {
    pending_.reset();
    return;
  }
  if (pending_ == song) return;
  if (start == Start::NextBar && song_ && !ended_) {
    pending_ = std::move(song);
    const double bar = song_->ticksPerBar();
    switchTick_ = std::ceil(tick_ / bar - kEpsilon) * bar;
    return;
  }
  pending_.reset();
  releaseAll();
  begin(std::move(song), 0.0);
}

void Player::stop() {
  pending_.reset();
  if (idle() || fadeLeft_) return;
  const float fade = kFadeMs * static_cast<float>(rate_) / 1000.0f;
  fadeLeft_ = std::max<uint32_t>(1, static_cast<uint32_t>(fade));
  fadeStep_ = 1.0f / static_cast<float>(fadeLeft_);
}

bool Player::idle() const {
  if (fadeLeft_) return false;
  if (song_ && !ended_) return false;
  for (const Voice& v : voices_)
    if (v.active) return false;
  return echoTail_ == 0;
}

bool Player::beat(double& out) const {
  if (!song_ || ended_ || fadeLeft_) return false;
  out = tick_ / kTicksPerBeat;
  return true;
}

void Player::begin(std::shared_ptr<const Song> song, double tick) {
  song_ = std::move(song);
  samplesPerTick_ = static_cast<double>(rate_) * song_->secondsPerTick();
  cursors_.assign(song_->tracks.size(), Cursor());
  ended_ = false;
  configureEcho();
  seek(tick);
}

void Player::seek(double tick) {
  tick_ = tick;
  for (std::size_t t = 0; t < cursors_.size(); ++t) {
    const SongVector<Note>& notes = song_->tracks[t].notes;
    cursors_[t].next = static_cast<std::size_t>(
        std::lower_bound(notes.begin(), notes.end(), tick - kEpsilon,
                         [](const Note& n, double at) { return n.tick < at; }) -
        notes.begin());
  }
}

void Player::configureEcho() {
  echoOn_ = song_->echoSixteenths > 0.0f;
  if (!echoOn_) return;
  const double seconds = song_->echoSixteenths * kTicksPerSixteenth * song_->secondsPerTick();
  const std::size_t delay =
      std::max<std::size_t>(1, static_cast<std::size_t>(std::lround(seconds * rate_)));
  if (echo_.size() < delay + 1) {
    std::vector<float> longer(delay + 1, 0.0f);
    for (std::size_t i = 0; i < echo_.size(); ++i)
      longer[i] = echo_[(echoWrite_ + i) % echo_.size()];
    echoWrite_ = echo_.size() % longer.size();
    echo_.swap(longer);
  }
  echoDelay_ = delay;
  echoFeedback_ = song_->echoFeedback;
  echoDamp_ = 1.0f - std::exp(-2.0f * kPi * song_->echoDampHz / static_cast<float>(rate_));
}

Player::Voice& Player::allocate() {
  Voice* quietest = nullptr;
  Voice* oldest = &voices_[0];
  for (Voice& v : voices_) {
    if (!v.active) return v;
    if (v.stage == Voice::Stage::Release && (!quietest || v.level < quietest->level)) quietest = &v;
    if (v.order < oldest->order) oldest = &v;
  }
  return quietest ? *quietest : *oldest;
}

void Player::noteOn(std::size_t track, const Note& note) {
  Cursor& cursor = cursors_[track];
  const Patch& patch = song_->instruments[note.instrument].patch;
  const uint32_t gate = std::max<uint32_t>(
      1, static_cast<uint32_t>(std::lround(note.length * patch.gate * samplesPerTick_)));
  const float glide = std::max(1.0f, patch.glideMs * static_cast<float>(rate_) / 1000.0f);
  if (note.slide && cursor.voice >= 0) {
    Voice& v = voices_[static_cast<std::size_t>(cursor.voice)];
    if (v.active && v.track == track && v.order == cursor.order) {
      v.glideFrom = v.current;
      v.pitch = static_cast<float>(note.pitch);
      v.glideLeft = v.glideTotal = glide;
      v.gateLeft = gate;
      v.gated = true;
      v.gain = kVoiceGain * v.patch.volume * static_cast<float>(note.velocity) / 100.0f;
      if (v.stage == Voice::Stage::Release) {
        v.stage = Voice::Stage::Decay;
        v.level = std::max(v.level, v.patch.sustain);
      }
      cursor.pitch = static_cast<float>(note.pitch);
      return;
    }
  }
  Voice& v = allocate();
  noiseSeed_ = noiseSeed_ * 1664525u + 1013904223u;
  v.start(patch, note, track, ++order_, gate, noiseSeed_, rate_);
  if (note.slide && cursor.pitch >= 0.0f) {
    v.glideFrom = v.current = cursor.pitch;
    v.glideLeft = v.glideTotal = glide;
  }
  cursor.voice = static_cast<int>(&v - voices_.data());
  cursor.order = v.order;
  cursor.pitch = static_cast<float>(note.pitch);
}

void Player::fire() {
  for (std::size_t t = 0; t < cursors_.size(); ++t) {
    const SongVector<Note>& notes = song_->tracks[t].notes;
    Cursor& cursor = cursors_[t];
    while (cursor.next < notes.size() && notes[cursor.next].tick <= tick_ + kEpsilon)
      noteOn(t, notes[cursor.next++]);
  }
}

double Player::nextEventTick() const {
  double next = song_->endOf(effect_);
  if (pending_) next = std::min(next, switchTick_);
  for (std::size_t t = 0; t < cursors_.size(); ++t) {
    const SongVector<Note>& notes = song_->tracks[t].notes;
    if (cursors_[t].next < notes.size())
      next = std::min(next, static_cast<double>(notes[cursors_[t].next].tick));
  }
  return next;
}

void Player::releaseAll() {
  for (Voice& v : voices_) v.release(rate_);
}

void Player::renderVoices(float* out, std::size_t frames) {
  std::fill(send_.begin(), send_.begin() + static_cast<std::ptrdiff_t>(frames), 0.0f);
  for (Voice& v : voices_)
    if (v.active) v.render(out, send_.data(), frames, rate_);
  if (!echoDelay_) return;
  float* line = echo_.data();
  const float* send = send_.data();
  const std::size_t size = echo_.size();
  const float damp = echoDamp_;
  const float feedback = echoFeedback_;
  const bool on = echoOn_;
  float low = echoLow_;
  float loudest = 0.0f;
  std::size_t write = echoWrite_;
  std::size_t read = (write + size - echoDelay_) % size;
  for (std::size_t i = 0; i < frames; ++i) {
    low = flush(low + damp * (line[read] - low));
    const float in = on ? send[i] : 0.0f;
    line[write] = in + low * feedback;
    if (++read == size) read = 0;
    if (++write == size) write = 0;
    out[i] += low;
    loudest = std::max(loudest, std::fabs(low) + std::fabs(in));
  }
  echoLow_ = low;
  echoWrite_ = write;
  // The line is quiet once a whole delay passed without anything in it.
  if (loudest > kSilent) {
    echoTail_ = static_cast<uint32_t>(echoDelay_ + 1);
  } else if (echoTail_) {
    echoTail_ = echoTail_ > frames ? echoTail_ - static_cast<uint32_t>(frames) : 0;
  }
}

bool Player::sequencing() const { return song_ && !ended_ && !fadeLeft_; }

// A pending song takes over at its bar line; at the end the song loops or is over. Checked as
// soon as the head gets there, so beat() and idle() never report a position past the end.
void Player::boundary() {
  if (!sequencing()) return;
  if (pending_ && tick_ >= switchTick_ - kEpsilon) {
    const double overshoot = (tick_ - switchTick_) * samplesPerTick_;
    std::shared_ptr<const Song> next = std::move(pending_);
    pending_.reset();
    releaseAll();
    begin(std::move(next), 0.0);
    tick_ = overshoot / samplesPerTick_;
  }
  const double end = song_->endOf(effect_);
  if (tick_ < end - kEpsilon) return;
  if (song_->loops && !effect_) {
    seek(song_->loopTick + (tick_ - end));
  } else {
    ended_ = true;
    releaseAll();
  }
}

void Player::render(float* out, std::size_t frames) {
  std::fill(out, out + frames, 0.0f);
  std::size_t pos = 0;
  while (pos < frames) {
    std::size_t n = std::min(frames - pos, kMaxBlock);
    boundary();
    if (sequencing()) {
      fire();
      const double ahead = (nextEventTick() - tick_) * samplesPerTick_;
      const auto due = static_cast<std::size_t>(std::ceil(ahead - kEpsilon));
      n = std::min(n, std::max<std::size_t>(1, due));
    }
    renderVoices(out + pos, n);
    if (sequencing()) {
      tick_ += static_cast<double>(n) / samplesPerTick_;
      boundary();
    }
    pos += n;
  }
  const float volume = song_ ? song_->volume : 1.0f;
  if (!fadeLeft_) {
    for (std::size_t i = 0; i < frames; ++i) out[i] = soft(out[i] * volume);
    return;
  }
  for (std::size_t i = 0; i < frames; ++i) {
    float y = soft(out[i] * volume);
    if (fadeLeft_) {
      y *= fadeGain_;
      fadeGain_ = std::max(0.0f, fadeGain_ - fadeStep_);
      if (--fadeLeft_ == 0) {
        for (Voice& v : voices_) v.active = false;
        std::fill(echo_.begin(), echo_.end(), 0.0f);
        echoLow_ = 0.0f;
        echoTail_ = 0;
        echoDelay_ = 0;
        echoOn_ = false;
        song_.reset();
        ended_ = true;
        fadeGain_ = 1.0f;
        for (std::size_t rest = i + 1; rest < frames; ++rest) out[rest] = 0.0f;
        out[i] = y;
        return;
      }
    }
    out[i] = y;
  }
}

}
}
