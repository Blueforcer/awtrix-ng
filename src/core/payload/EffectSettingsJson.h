#pragma once

#include "core/api/JsonReader.h"

#include "core/effects/IEffect.h"

namespace awtrix {
namespace payload {

constexpr float kSpeedMin = 0.1f;
constexpr float kSpeedMax = 10.0f;

bool readEffectSettings(api::JsonReader r, EffectSettings& out);

}
}
