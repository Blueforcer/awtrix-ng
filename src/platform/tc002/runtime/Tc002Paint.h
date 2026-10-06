#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>

#include "core/render/Canvas.h"
#include "core/render/Color.h"
#include "core/render/Font.h"
#include "core/render/Motion.h"
#include "core/render/TextRenderer.h"

// Pixel helpers shared by the TC002's own screens.
namespace awtrix::paint {

using color::scale;

inline int nearest(float value) { return static_cast<int>(std::lround(value)); }

inline uint32_t whiten(uint32_t rgb, float k) { return color::lerp(rgb, 0xFFFFFFu, motion::clamp01(k)); }

inline constexpr float kMinFlash = 0.2f;
inline constexpr int kShake[6] = {1, -1, 1, -1, 1, 0};

inline float flashLevel(float k) { return k < kMinFlash ? 0.0f : motion::clamp01(k); }
inline uint32_t flash(uint32_t rgb, float k) { return k < kMinFlash ? rgb : whiten(rgb, k); }

inline void plot(Canvas& c, float x, float y, uint32_t rgb) {
  c.setPixel(nearest(x), nearest(y), rgb);
}

enum class ParticleKind : uint8_t { None, Spark, Burst, Drop };
struct Particle {
  ParticleKind kind = ParticleKind::None;
  int64_t at = 0;
  int x = 0, y = 0, count = 0;
  uint32_t color = 0;
};

template <std::size_t N>
class ParticleRing {
 public:
  void add(const Particle& particle) {
    particles_[next_] = particle;
    next_ = (next_ + 1) % N;
  }
  void clear() { particles_ = {}; next_ = 0; }
  auto begin() const { return particles_.begin(); }
  auto end() const { return particles_.end(); }
 private:
  std::array<Particle, N> particles_{};
  std::size_t next_ = 0;
};

enum class ParticleStyle { QuickSettings, Status };

template <ParticleStyle style>
inline uint32_t particleColor(uint32_t rgb, float t) {
  if constexpr (style == ParticleStyle::Status) return whiten(rgb, 1.0f - t);
  return color::lerp(0xFFFFFFu, rgb, t);
}

template <ParticleStyle style>
void drawSpark(Canvas& c, const Particle& e, float age) {
  for (int i = 0; i < 3; ++i) {
    const float life = style == ParticleStyle::Status
        ? 220.0f + 60.0f * motion::noise(e.at, i) : 260.0f + 80.0f * motion::noise(e.at, i);
    if (age > life) continue;
    const float vx = style == ParticleStyle::Status
        ? (motion::noise(e.at, 10 + i) - 0.5f) * 0.04f : 0.012f + 0.03f * motion::noise(e.at, 10 + i);
    const float vy = style == ParticleStyle::Status
        ? 0.01f + 0.015f * motion::noise(e.at, 20 + i) : -(0.015f + 0.03f * motion::noise(e.at, 20 + i));
    constexpr float gravity = style == ParticleStyle::Status ? 0.00006f : 0.00012f;
    const float t = age / life;
    plot(c, e.x + vx * age, e.y + vy * age + gravity * age * age,
         scale(particleColor<style>(e.color, t), 1.0f - t * t));
  }
}

template <ParticleStyle style>
void drawBurst(Canvas& c, const Particle& e, float age, float t) {
  if (t < 0 || t > 1.0f) return;
  for (int i = 0; i < 8; ++i) {
    const float angle = i * 6.2831853f / 8.0f + 0.3f * motion::noise(e.at, i);
    const float speed = (style == ParticleStyle::Status ? 0.03f : 0.035f) + 0.02f * motion::noise(e.at, 10 + i);
    plot(c, e.x + std::cos(angle) * speed * age, e.y + std::sin(angle) * speed * age + 0.00008f * age * age,
         scale(particleColor<style>(e.color, t), 1.0f - t));
  }
}

template <ParticleStyle style>
void drawDrop(Canvas& c, const Particle& e, float age) {
  constexpr float life = style == ParticleStyle::Status ? 440.0f : 420.0f;
  for (int i = 0; i < e.count; ++i) {
    const float falling = age - 70.0f * motion::noise(e.at, i);
    if (falling > life) continue;
    const float drop = falling < 0.0f ? 0.0f : 0.00035f * falling * falling;
    const uint32_t rgb = scale(e.color, 0.85f * (1.0f - motion::clamp01(falling / life)));
    for (int h = 0; h < 3; ++h) plot(c, static_cast<float>(e.x + i), e.y + h + drop, rgb);
  }
}

template <ParticleStyle style, std::size_t N>
void drawParticles(Canvas& c, const ParticleRing<N>& particles, int64_t now) {
  for (const Particle& e : particles) {
    const float age = static_cast<float>(now - e.at);
    if (age < 0) continue;
    switch (e.kind) {
      case ParticleKind::Spark: drawSpark<style>(c, e, age); break;
      case ParticleKind::Burst: drawBurst<style>(c, e, age, age / 280.0f); break;
      case ParticleKind::Drop: drawDrop<style>(c, e, age); break;
      case ParticleKind::None: break;
    }
  }
}

// Draws with the ink box, not the pen, at (x, top) and returns the ink width.
inline int drawInk(Canvas& c, const GfxFont& font, int x, int top, std::string_view s, uint32_t color) {
  const text::TextMetrics m = text::measureInk(font, s);
  text::drawText(c, font, x - m.inkLeft, top - m.inkTop, s, color);
  return m.inkWidth();
}

}
