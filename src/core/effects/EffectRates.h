#pragma once

namespace awtrix {

constexpr long kBaseStepMs = 24;

// Multipliers on the kBaseStepMs tick: kContinuous advances the frame counter once every 24 ms,
// the slower rates proportionally less often.
namespace rate {

constexpr float kDrift = 0.16f;
constexpr float kGentle = 0.24f;
constexpr float kSteady = 0.36f;
constexpr float kBrisk = 0.60f;
constexpr float kContinuous = 1.00f;

constexpr float kStatic = 1.00f;

}

// Radians of phase per frame step for the sine-driven effects: one full turn every ~131 steps.
constexpr float kPhasePerStep = 0.048f;

}
