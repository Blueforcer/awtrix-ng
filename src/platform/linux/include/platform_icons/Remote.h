#pragma once

#include "media/RemoteImages.h"

namespace awtrix {

struct RemoteIconEntry {
  uint32_t remoteSeen = 0;
  uint8_t padX = 0;
  uint8_t padY = 0;
  void resetRemote() { remoteSeen = 0; padX = padY = 0; }
  int offsetX() const { return padX; }
  int offsetY() const { return padY; }
};

class RemoteIconSource {
 public:
  void setRemoteImages(media::IRemoteImages* remote) { remote_ = remote; }
  media::IRemoteImages* remote() const { return remote_; }
  bool remoteChanged(const RemoteIconEntry& entry) const {
    return remote_ && remote_->generation() != entry.remoteSeen;
  }

 private:
  media::IRemoteImages* remote_ = nullptr;
};

}
