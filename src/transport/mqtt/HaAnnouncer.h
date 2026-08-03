#pragma once

#include <PubSubClient.h>

#include <string>

#include "core/mqtt/HaDiscovery.h"
#include "persistence/DeviceConfig.h"

namespace awtrix {

class IBoard;

class HaAnnouncer {
 public:
  void configure(const DeviceConfig& cfg, IBoard& board, const std::string& uid,
                 const std::string& prefix, const std::string& hostname);
  void announce(PubSubClient& client);

  bool enabled() const { return enabled_; }

 private:
  ha::DiscoveryContext ctx_;
  std::string announcedTopic_;
  bool enabled_ = false;
};

}
