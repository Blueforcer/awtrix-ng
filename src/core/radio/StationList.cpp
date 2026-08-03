#include "core/radio/StationList.h"

#include <string>
#include <string_view>

#include "core/api/JsonReader.h"

namespace awtrix {
namespace radio {

namespace {

bool schemeIsHttp(const std::string& url) {
  return url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0;
}

bool readOne(api::JsonReader entry, int index, Station& out, StationError& error) {
  if (!entry.isObject()) {
    error = {"stations", "each entry must be an object", index};
    return false;
  }
  std::string name, url;
  api::JsonReader r = entry;
  if (!r.enterObject()) {
    error = {"stations", "each entry must be an object", index};
    return false;
  }
  while (r.nextMember()) {
    if (r.keyEquals("name")) r.appendString(name);
    else if (r.keyEquals("url")) r.appendString(url);
    if (!r.skipValue()) break;
  }
  if (name.empty()) {
    error = {"name", "must not be empty", index};
    return false;
  }
  if (url.empty()) {
    error = {"url", "must not be empty", index};
    return false;
  }

  out.name = name;
  out.url = url;
  if (out.name.size() > kMaxNameLength) {
    error = {"name", "must be at most 24 characters", index};
    return false;
  }
  if (out.url.size() > kMaxUrlLength) {
    error = {"url", "must be at most 255 characters", index};
    return false;
  }
  if (!schemeIsHttp(out.url)) {
    error = {"url", "must start with http:// or https://", index};
    return false;
  }
  return true;
}

}

// Accepts either a bare array or an object with a "stations" member, because both shapes reach
// this from the API. out is only replaced once the whole list validates.
bool parseStations(const std::string& json, std::vector<Station>& out, StationError& error) {
  if (!api::isWellFormed(json)) {
    error = {"", "invalid JSON", -1};
    return false;
  }

  api::JsonReader array{std::string_view(json)};
  if (array.isObject()) {
    array = api::memberValue(array, "stations");
    if (!array.isArray()) {
      error = {"stations", "must be an array", -1};
      return false;
    }
  } else if (!array.isArray()) {
    error = {"stations", "must be an array", -1};
    return false;
  }

  // Count first so an oversized list is refused before any Station strings are allocated.
  std::size_t count = 0;
  {
    api::JsonReader r = array;
    if (!r.enterArray()) return false;
    while (r.nextElement()) {
      ++count;
      if (!r.skipValue()) break;
    }
    if (!r.ok()) {
      error = {"stations", "must be an array", -1};
      return false;
    }
  }
  if (count > kMaxStations) {
    error = {"stations", "at most 32 stations", -1};
    return false;
  }

  std::vector<Station> parsed;
  parsed.reserve(count);
  int index = 0;
  if (!array.enterArray()) return false;
  while (array.nextElement()) {
    Station station;
    if (!readOne(array, index, station, error)) return false;
    for (const Station& existing : parsed) {
      if (existing.name == station.name) {
        error = {"name", "duplicate station name", index};
        return false;
      }
    }
    parsed.push_back(std::move(station));
    ++index;
    if (!array.skipValue()) return false;
  }
  if (!array.ok()) {
    error = {"stations", "must be an array", -1};
    return false;
  }

  out.swap(parsed);
  return true;
}

std::string stationsToJson(const std::vector<Station>& stations) {
  std::string out = "{\"stations\":[";
  bool first = true;
  for (const Station& station : stations) {
    if (!first) out += ',';
    first = false;
    out += "{\"name\":\"";
    for (char c : station.name) {
      if (c == '"' || c == '\\') out += '\\';
      out += c;
    }
    out += "\",\"url\":\"";
    for (char c : station.url) {
      if (c == '"' || c == '\\') out += '\\';
      out += c;
    }
    out += "\"}";
  }
  out += "]}";
  return out;
}

int indexOfStation(const std::vector<Station>& stations, const std::string& name) {
  for (std::size_t i = 0; i < stations.size(); ++i)
    if (stations[i].name == name) return static_cast<int>(i);
  return -1;
}

}
}
