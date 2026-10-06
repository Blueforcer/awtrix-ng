#pragma once

#include <string>
#include <utility>

#include "core/radio/PlaylistParser.h"
#include "core/radio/StreamPolicy.h"

namespace awtrix::radio {

// The URL a station is fetched from next: the station's own URL, or the stream its playlist named.
class StationUrl {
 public:
  explicit StationUrl(std::string station = {}) : station_(std::move(station)), current_(station_) {}

  const std::string& current() const { return current_; }

  // A playlist came from current(). True when it named a stream to fetch next. False when it named
  // none or kMaxPlaylistHops playlists came in a row; the next fetch is then the station again.
  bool follow(const std::string& playlist) {
    std::string next;
    if (++hops_ <= kMaxPlaylistHops && parsePlaylist(playlist, next)) {
      current_ = std::move(next);
      return true;
    }
    restart();
    return false;
  }

  // Back to the station's own URL, so its playlist is read again with fresh session tokens.
  void restart() {
    current_ = station_;
    hops_ = 0;
  }

 private:
  std::string station_;
  std::string current_;
  int hops_ = 0;
};

}
