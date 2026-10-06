#pragma once

#include "core/sound/Sound.h"
#include "core/sound/SoundSpec.h"

namespace awtrix::sound {

enum class Stop : uint8_t { All, Alert, App, Radio, ScriptSounds, ScriptMusic };
enum class PlayAs : uint8_t { Once, Effect };

struct Caps {
  bool mp3 = false;
  bool rtttl = false;
  bool song = false;
  bool speech = false;
  bool track = false;
  bool radio = false;
  bool url = false;
  bool effect = false;
  bool clip = false;
};

// The router resolves stored files and decides whether an alert permits a new one-shot.
struct PcmRequest {
  const Spec& spec;
  const std::string& path;
  const std::string& owner;
  Group group;
  PlayAs as;
  bool allowOneShot;
};

enum class PcmPlay : uint8_t { OneShot, Layer, Ignored, Unavailable, Invalid };

struct PcmState {
  bool oneShot = false;
  bool groupKnown = false;
  Group group = Group::Alert;
  bool effects = false;
  bool music = false;
};

struct PcmError {
  Group group = Group::Alert;
  bool stopRepeat = false;
  std::string message;
};

}
