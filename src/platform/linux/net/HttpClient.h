#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace httplib {
class Client;
class Result;
struct Request;
}

namespace awtrix::net {

// "http(s)://host[:port]" with the scheme in lower case, and the path with its query; false for
// anything else.
bool splitUrl(const std::string& url, std::string& origin, std::string& target);

struct ClientLimits {
  // From connecting to the last byte, redirects included; zero permits an endless stream.
  int64_t timeoutMs = 15000;
  int connectTimeoutSec = 3;
  bool followRedirects = false;
  int ioTimeoutSec = 5;
};

// The timeouts, certificate checks and redirect policy every outgoing request of the runtime
// shares.
void configure(httplib::Client& client, const ClientLimits& limits);

// httplib starts the clock of its total timeout only in its own Get(), Post() and so on; a
// request handed to send() needs it started here.
void startClock(httplib::Request& request);

using BodyReceiver = std::function<bool(const char*, std::size_t)>;
using RequestHeaders = std::vector<std::pair<std::string, std::string>>;

// Any method, with a total timeout and at most 1 MiB delivered to the receiver.
httplib::Result send(httplib::Client& client, const std::string& method,
                     const std::string& path, const std::string& body,
                     const RequestHeaders& headers, const BodyReceiver& receive);

struct BodyPolicy {
  std::size_t maxBytes;
  std::function<bool()> active;
  // Checks the announced size (when present) and then each incoming chunk. This checks
  // available space; it must not reserve it twice.
  std::function<bool(std::size_t)> room;
};
struct ReceivedBody {
  int status = 0;
  std::size_t size = 0;
  bool tooLarge = false;
};

// Accepts only status 200; enforces both Content-Length and the actual received size.
// Client configuration, interruption and mapping failures to public errors remain with callers.
httplib::Result receive200(httplib::Client& client, const std::string& path,
                           const BodyPolicy& policy, ReceivedBody& state,
                           const BodyReceiver& receive);

// Ends a request from another thread wherever it is. The socket in use is shut down through a
// duplicate, which also reaches the client httplib makes for a redirect to another host, and every
// socket opened after it is shut at once, until rearm().
class SocketInterrupt {
 public:
  SocketInterrupt() = default;
  SocketInterrupt(const SocketInterrupt&) = delete;
  SocketInterrupt& operator=(const SocketInterrupt&) = delete;
  ~SocketInterrupt();

  // Before the request: hands the client's sockets to this interrupt. It must outlive the client.
  void watch(httplib::Client& client);
  void interrupt();
  // Before the next request: lets new sockets live again.
  void rearm();
  // After a request: lets go of its socket.
  void release();

 private:
  void adopt(int socket);
  void closeCopy();

  std::mutex mutex_;
  int socket_ = -1;
  bool interrupted_ = false;
};

}
