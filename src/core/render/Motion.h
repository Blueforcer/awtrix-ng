#pragma once

#include <cmath>
#include <cstdint>

// Easing curves and a deterministic noise source for frame animations. Every function is pure, so
// an animation built from them is a function of its start time and the frame time.
namespace awtrix::motion {

inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// Fraction of `ms` elapsed since `at`; below 0 before the start and above 1 after the end.
inline float progress(int64_t now, int64_t at, float ms) { return static_cast<float>(now - at) / ms; }

// 1 at `at`, falling linearly to 0 over `ms`; 0 before `at`.
inline float pulse(int64_t now, int64_t at, float ms) {
  return now < at ? 0.0f : clamp01(1.0f - progress(now, at, ms));
}

inline float pulse(float now, float at, float ms) {
  return now < at ? 0.0f : clamp01(1.0f - (now - at) / ms);
}

inline float easeIn(float t) {
  t = clamp01(t);
  return t * t;
}

inline float easeOutCubic(float t) {
  const float u = 1.0f - t;
  return 1.0f - u * u * u;
}

inline float easeOut(float t) { return easeOutCubic(clamp01(t)); }
inline float smoothstep(float t) { return t * t * (3.0f - 2.0f * t); }
inline float easeInOut(float t) { return smoothstep(clamp01(t)); }

// A ball dropped onto the floor: reaches 1 first at t = 0.36 and settles there with three bounces.
inline float bounce(float t) {
  t = clamp01(t);
  constexpr float n = 7.5625f, d = 2.75f;
  if (t < 1.0f / d) return n * t * t;
  if (t < 2.0f / d) { t -= 1.5f / d; return n * t * t + 0.75f; }
  if (t < 2.5f / d) { t -= 2.25f / d; return n * t * t + 0.9375f; }
  t -= 2.625f / d;
  return n * t * t + 0.984375f;
}

// A slow cosine between 1 at t = 0 and `low` half a period later, repeating every `period` ms.
inline float breathe(int64_t t, int64_t period, float low) {
  const float phase = static_cast<float>(t % period) / static_cast<float>(period);
  return low + (1.0f - low) * 0.5f * (1.0f + std::cos(6.2831853f * phase));
}

// Uniform in [0, 1), fixed for each (seed, salt) pair.
inline float noise(int64_t seed, int salt) {
  uint32_t h = static_cast<uint32_t>(seed) * 2654435761u ^ static_cast<uint32_t>(salt) * 0x9E3779B9u;
  h ^= h >> 15;
  h *= 0x2C1B3C6Du;
  h ^= h >> 12;
  h *= 0x297A2D39u;
  h ^= h >> 15;
  return static_cast<float>(h & 0xFFFFFFu) / 16777216.0f;
}

}
