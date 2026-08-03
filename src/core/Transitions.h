#pragma once

#include <cstddef>
#include <string>

namespace awtrix {

// Random is not an effect of its own: resolveTransition turns it into one of the others, picked
// fresh for every transition. Order is part of the API, so append new effects at the end.
enum class Transition : int {
  Random = 0,
  Slide,
  Dim,
  Zoom,
  Rotate,
  Pixelate,
  Curtain,
  Ripple,
  Blink,
  Reload,
  Fade,
  Cover,
  Uncover,
  Split,
  Blinds,
  Blocks,
  Flash,
  Diamond,
  Wave,
  Rain,
  Melt,
  Interlace,
  Count,
};

inline constexpr const char* kTransitionNames[] = {
    "Random", "Slide",  "Dim",    "Zoom",   "Rotate", "Pixelate",  "Curtain",
    "Ripple", "Blink",  "Reload", "Fade",   "Cover",  "Uncover",   "Split",
    "Blinds", "Blocks", "Flash",  "Diamond", "Wave",  "Rain",      "Melt",
    "Interlace",
};
inline constexpr std::size_t kTransitionCount =
    sizeof(kTransitionNames) / sizeof(kTransitionNames[0]);

static_assert(kTransitionCount == static_cast<std::size_t>(Transition::Count),
              "kTransitionNames and enum Transition drifted apart");

enum class Pacing : int { Eased, Linear };

inline constexpr Pacing kTransitionPacing[] = {
    Pacing::Eased,
    Pacing::Eased,
    Pacing::Linear,
    Pacing::Linear,
    Pacing::Eased,
    Pacing::Linear,
    Pacing::Eased,
    Pacing::Eased,
    Pacing::Linear,
    Pacing::Eased,
    Pacing::Linear,
    Pacing::Eased,
    Pacing::Eased,
    Pacing::Eased,
    Pacing::Eased,
    Pacing::Linear,
    Pacing::Linear,
    Pacing::Eased,
    Pacing::Eased,
    Pacing::Eased,
    Pacing::Eased,
    Pacing::Eased,
};

static_assert(sizeof(kTransitionPacing) / sizeof(kTransitionPacing[0]) == kTransitionCount,
              "kTransitionPacing and enum Transition drifted apart");

inline constexpr Pacing transitionPacing(Transition t) {
  const int i = static_cast<int>(t);
  return (i >= 0 && i < static_cast<int>(kTransitionCount)) ? kTransitionPacing[i] : Pacing::Eased;
}

inline std::string transitionsJson() {
  std::string out = "[";
  for (std::size_t i = 0; i < kTransitionCount; ++i) {
    if (i) out += ',';
    out += '"';
    out += kTransitionNames[i];
    out += '"';
  }
  out += ']';
  return out;
}

inline std::string transitionsOptions() {
  std::string out;
  for (std::size_t i = 0; i < kTransitionCount; ++i) {
    if (i) out += ';';
    out += kTransitionNames[i];
  }
  return out;
}

}
