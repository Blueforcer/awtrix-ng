#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "platform/linux/ble/BleTypes.h"

// The Apple Media Service: what the phone's player plays, pushed as entity updates once asked
// for, and the remote commands it takes.
namespace awtrix::ble::ams {

constexpr const char* kService = "89d3502b-0f36-433a-8ef4-c502ad55f8dc";
constexpr const char* kEntityUpdate = "2f7cabce-808d-411f-9a0c-bb92ba96c102";
constexpr const char* kEntityAttribute = "c6b2f38c-23ab-46d8-a6ab-a3a870bbd5d7";

enum Entity : uint8_t { kPlayer = 0, kQueue = 1, kTrack = 2 };
enum PlayerAttribute : uint8_t { kPlayerName = 0, kPlaybackInfo = 1 };
enum TrackAttribute : uint8_t { kArtist = 0, kAlbum = 1, kTitle = 2, kDuration = 3 };

// Entity Update writes that ask for the track's artist, title and duration, and for the player's
// name and playback info.
Bytes trackAttributes();
Bytes playerAttributes();

struct Update {
  uint8_t entity = 0;
  uint8_t attribute = 0;
  bool truncated = false;
  std::string value;
};

// One Entity Update value; false when it is shorter than its three header bytes.
bool parseUpdate(const Bytes& value, Update& out);

struct PlaybackInfo {
  // 0 paused, 1 playing, 2 rewinding, 3 fast-forwarding.
  int state = 0;
  double rate = 0;
  double elapsed = 0;
};

// "state,rate,elapsed" as the phone sends it; empty fields, as with nothing loaded, read as 0.
bool parsePlaybackInfo(std::string_view text, PlaybackInfo& out);

// The player as far as the updates told: elapsed is the position at elapsedAtMs, which moves on
// by rate while it plays.
struct Playback {
  std::string player, artist, title;
  double duration = 0;
  PlaybackInfo info;
  int64_t elapsedAtMs = 0;

  bool playing() const { return info.state != 0 && !title.empty(); }
  // Seconds into the track at nowMs, never past its end.
  double elapsedAt(int64_t nowMs) const;
  // False when the update changed nothing that is shown.
  bool apply(const Update& update, int64_t nowMs);
};

}
