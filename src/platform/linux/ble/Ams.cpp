#include "platform/linux/ble/Ams.h"

#include <algorithm>

#include "core/api/JsonReader.h"

namespace awtrix::ble::ams {
namespace {

double number(std::string_view text) {
  double v = 0;
  return !text.empty() && api::parseDouble(text.data(), text.data() + text.size(), v) ? v : 0;
}

}

Bytes trackAttributes() { return Bytes{kTrack, kArtist, kTitle, kDuration}; }
Bytes playerAttributes() { return Bytes{kPlayer, kPlayerName, kPlaybackInfo}; }

bool parseUpdate(const Bytes& value, Update& out) {
  if (value.size() < 3) return false;
  out.entity = value[0];
  out.attribute = value[1];
  out.truncated = value[2] & 1;
  out.value.assign(reinterpret_cast<const char*>(value.data() + 3), value.size() - 3);
  return true;
}

bool parsePlaybackInfo(std::string_view text, PlaybackInfo& out) {
  std::string_view fields[3];
  for (std::string_view& field : fields) {
    const std::size_t comma = text.find(',');
    field = text.substr(0, comma);
    text = comma == std::string_view::npos ? std::string_view() : text.substr(comma + 1);
  }
  const double state = number(fields[0]);
  if (state < 0 || state > 3) return false;
  out.state = static_cast<int>(state);
  out.rate = number(fields[1]);
  out.elapsed = std::max(0.0, number(fields[2]));
  return true;
}

double Playback::elapsedAt(int64_t nowMs) const {
  double at = info.elapsed;
  if (playing()) at += info.rate * static_cast<double>(nowMs - elapsedAtMs) / 1000.0;
  at = std::max(0.0, at);
  return duration > 0 ? std::min(at, duration) : at;
}

bool Playback::apply(const Update& update, int64_t nowMs) {
  std::string* text = nullptr;
  if (update.entity == kPlayer && update.attribute == kPlayerName) text = &player;
  else if (update.entity == kTrack && update.attribute == kArtist) text = &artist;
  else if (update.entity == kTrack && update.attribute == kTitle) text = &title;
  if (text) {
    if (*text == update.value) return false;
    *text = update.value;
    return true;
  }
  if (update.entity == kTrack && update.attribute == kDuration) {
    const double seconds = std::max(0.0, number(update.value));
    if (seconds == duration) return false;
    duration = seconds;
    return true;
  }
  if (update.entity == kPlayer && update.attribute == kPlaybackInfo) {
    PlaybackInfo next;
    if (!parsePlaybackInfo(update.value, next)) return false;
    info = next;
    elapsedAtMs = nowMs;
    return true;
  }
  return false;
}

}
