#pragma once

#include <PubSubClient.h>

#include <string>

#include "core/mqtt/HaDiscovery.h"
#include "core/sound/AudioRouter.h"
#include "persistence/DeviceConfig.h"

namespace awtrix {

class IBoard;

class HaAnnouncer {
 public:
  void configure(const DeviceConfig& cfg, IBoard& board, const sound::Caps& audio,
                 const std::string& uid, const std::string& prefix, const std::string& hostname);
  void announce(PubSubClient& client);
  void setPlatformEntities(const ha::Entity* entities, std::size_t count) {
    ctx_.setPlatformEntities(entities, count);
  }
  void setUrl(std::string url) { ctx_.url = std::move(url); }

  bool enabled() const { return enabled_; }

 private:
  ha::DiscoveryContext ctx_;
  std::string announcedTopic_;
  bool enabled_ = false;
};

}
