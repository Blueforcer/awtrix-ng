#pragma once

#include <cstdint>
#include <string>

#include "core/api/JsonReader.h"
#include "core/api/JsonWriter.h"

namespace awtrix {

enum class ScrollMode : uint8_t { Static, Wrap, Loop, Bounce };
enum class ScrollDirection : uint8_t { Left, Right };
enum class ScrollEntry : uint8_t { Inline, Offscreen };
enum class ScrollWhenFits : uint8_t { Static, Scroll };

struct ScrollDefaults {
  ScrollMode mode = ScrollMode::Wrap;
  ScrollDirection direction = ScrollDirection::Left;
  ScrollEntry entry = ScrollEntry::Inline;
  ScrollWhenFits whenFits = ScrollWhenFits::Static;
  int speed = 100;
  int gap = 8;
  int holdMs = 1000;
};

// Each has* flag records that the payload actually set the field; the rest fall back to the
// device-wide ScrollDefaults.
struct ScrollSpec {
  bool hasMode = false;
  ScrollMode mode = ScrollMode::Wrap;
  bool hasDirection = false;
  ScrollDirection direction = ScrollDirection::Left;
  bool hasEntry = false;
  ScrollEntry entry = ScrollEntry::Inline;
  bool hasWhenFits = false;
  ScrollWhenFits whenFits = ScrollWhenFits::Static;
  bool hasSpeed = false;
  int speed = 100;
  bool hasGap = false;
  int gap = 8;
  bool hasHoldMs = false;
  int holdMs = 1000;
};

namespace scroll {

struct Error {
  std::string field;
  std::string message;
};

bool read(api::JsonReader r, ScrollSpec& out, Error& err);

void write(api::JsonWriter& w, const ScrollDefaults& defaults);

bool parseMode(const char* value, ScrollMode& out);
bool parseDirection(const char* value, ScrollDirection& out);
bool parseEntry(const char* value, ScrollEntry& out);
bool parseWhenFits(const char* value, ScrollWhenFits& out);

}
}
