#pragma once

#include <functional>
#include <string>
#include <string_view>

// Commands accepted on the daemon's local control socket: SOCK_STREAM at
// /tmp/awtrix-tc002d/control.sock (mode 0600, peers must be uid 0), reached from a host through
// `adb forward tcp:N localfilesystem:<path>`. One request per connection: a 4-byte big-endian
// length followed by "<command>\n<payload>" (at most 4096 bytes); the reply uses the same framing,
// then the daemon closes the connection.
namespace awtrix {
namespace tc002d {

class ControlRegistry {
 public:
  using Handler = std::function<std::string(std::string_view payload)>;
  virtual ~ControlRegistry() = default;
  virtual void add(std::string command, Handler handler) = 0;
};

}
}
