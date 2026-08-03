#pragma once

#include <cstdint>
#include <string>
#include <vector>


namespace awtrix {
namespace rtttl {

struct Note {
  uint16_t frequency = 0;
  uint16_t duration = 0;
};

struct Parse {
  bool ok = false;
  std::string error;
  size_t index = 0;

  std::string title;
  uint16_t timeUnit = 0;
  std::vector<Note> notes;

  uint32_t durationMs() const;

  std::string describe() const;
};

// duration counts 64th notes and timeUnit is one 32nd note in ms, hence the halving.
inline uint32_t noteMs(uint16_t duration, uint16_t timeUnit) {
  return static_cast<uint32_t>(duration) * timeUnit / 2;
}

constexpr size_t kMaxLength = 512;
constexpr size_t kMaxTitle = 24;

Parse parse(const std::string& rtttl);

bool validName(const std::string& s);

bool retitle(const std::string& rtttl, const std::string& name, std::string& out);

}
}
