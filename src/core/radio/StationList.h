#pragma once

#include <string>
#include <vector>

namespace awtrix {
namespace radio {

struct Station {
  std::string name;
  std::string url;
};

inline constexpr std::size_t kMaxStations = 32;
inline constexpr std::size_t kMaxNameLength = 24;
inline constexpr std::size_t kMaxUrlLength = 255;

struct StationError {
  std::string field;
  std::string message;
  int index = -1;
};

bool parseStations(const std::string& json, std::vector<Station>& out, StationError& error);

std::string stationsToJson(const std::vector<Station>& stations);

int indexOfStation(const std::vector<Station>& stations, const std::string& name);

}
}
