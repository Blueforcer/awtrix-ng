#pragma once
#include <string>
#include <string_view>

#include "platform/tc002/voice/WebSocket.h"

namespace awtrix::tc002::voice {
struct Config {
  bool enabled = false;
  std::string url, token, pipeline, device;
};
// An origin only: credentials, paths, queries and fragments are rejected.
bool parseOrigin(std::string_view url, WebSocket::Endpoint& endpoint);
// Resolve a TTS URL without allowing another origin or a protocol-relative URL.
bool ttsTarget(const Config& config, std::string_view url, std::string& target);
class ConfigStore {
 public:
  explicit ConfigStore(std::string privateDirectory);
  bool load();
  // stored is false when the value was fine but could not be written.
  struct Rejection {
    bool stored = true;
    std::string field, message;
  };
  bool update(std::string_view json, Rejection& rejection);
  bool erase();
  std::string publicJson() const;
  const Config& get() const { return config_; }
  const std::string& directory() const { return directory_; }

 private:
  std::string directory_;
  Config config_;
};
}  // namespace awtrix::tc002::voice
