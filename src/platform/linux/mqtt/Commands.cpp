#include "transport/mqtt/MqttService.h"

namespace awtrix {
bool PlatformMqttCommands::platformMqttCommand(const std::string& topic, const std::string& body,
                                               std::string& result) {
  return platformCommands_ && platformCommands_(topic, body, result);
}

void MqttService::setPlatformCommands(PlatformCommands handle, const ha::Entity* entities,
                                      std::size_t count) {
  platformCommands_ = std::move(handle);
  ha_.setPlatformEntities(entities, count);
  if (link_.online()) announceHa();
}

}
