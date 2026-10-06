#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "core/synth/Song.h"

namespace awtrix {
namespace synth {

constexpr std::size_t kMaxSongBytes = 16384;
constexpr std::size_t kMaxTracks = 16;
constexpr std::size_t kMaxInstruments = 32;
constexpr std::size_t kMaxNotes = 8192;
constexpr std::size_t kMaxChord = 8;
constexpr uint32_t kMaxBars = 1024;
constexpr float kMaxEchoSeconds = 2.0f;

struct ParseResult {
  std::shared_ptr<const Song> song;
  std::string error;
  // Where the error is, both counted from 1; a new line starts after '\n'.
  std::size_t line = 0;
  std::size_t column = 0;

  bool ok() const { return song != nullptr; }
  // "reason (line 3, column 14)", or "" for a song that parsed.
  std::string describe() const;
};

// Song text: one statement per line, where ';' ends a line too. See docs/reference/songs.md.
ParseResult parse(const std::string& text);

}
}
