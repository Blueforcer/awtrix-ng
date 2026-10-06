#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "core/Command.h"

namespace awtrix {
namespace sound {

// The order is the order of the keys: the first one present names a "one sound key only" error.
enum class Kind : uint8_t { File, Rtttl, Song, Speech, Track, Station };

struct Spec {
  Kind kind = Kind::File;
  // The file value, the melody, song or speech text, or a station's name or address.
  std::string text;
  // A track number, or a station's position in the list; -1 when text holds the value.
  int number = -1;
  bool loop = false;
  bool nextBar = false;
};

constexpr std::size_t kMaxChoices = 4;
constexpr std::size_t kMaxSpeechBytes = 512;

// One sound object: a single entry, or a list whose first playable entry plays.
struct Choices {
  Spec items[kMaxChoices];
  uint8_t count = 0;

  const Spec* begin() const { return items; }
  const Spec* end() const { return items + count; }
  bool isStation() const { return count == 1 && items[0].kind == Kind::Station; }
};

// Where a sound object came from. It decides the keys allowed and how a field is named:
// a notification nests it under "sound", and only a script may ask for nextBar.
enum class Origin : uint8_t { Play, Notification, Script };

// A string, an object with exactly one source key, or a list of 1 to 4 of them. Checks everything
// that needs no hardware; song and speech text are judged by the sink that plays them.
bool parse(std::string_view json, Origin origin, Choices& out, DispatchDetail& detail);

// The field a mistake in entry index (-1 for a single entry) is reported under.
std::string specField(Origin origin, int index, const char* key);

// What GET /api/v1/audio names a playing sound by.
std::string displayName(const Spec& spec);

}
}
