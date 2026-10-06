#pragma once

#include "platform/posix/Resolver.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace awtrix::linux_script {

struct TcpEvent {
  enum class Kind { Open, Line, Closed };
  std::string app;
  uint32_t id = 0;
  Kind kind = Kind::Line;
  std::string data;
};

using TcpAddress = posix::SocketAddress;

// Line-based TCP connections for scripts. One poll() thread serves the sockets; names resolve on
// threads of their own, so a slow lookup never holds up another connection. Every connection
// queues its own events within a byte budget, and pop() takes them from the connections in turn.
// Closing or forgetting a connection is silent.
class TcpClients {
 public:
  static constexpr std::size_t kPerApp = 4;
  static constexpr std::size_t kTotal = 16;
  static constexpr std::size_t kLineMax = 16384;
  static constexpr std::size_t kOutMax = 65536;
  static constexpr std::size_t kInMax = 65536;
  static constexpr std::size_t kEventCost = 64;
  static constexpr std::size_t kAddresses = 16;

  // Resolves host and port to at most kAddresses addresses, in the order to try them; empty when
  // the name does not resolve. Runs on a lookup thread and may outlive the clients.
  using Lookup = std::function<std::vector<TcpAddress>(const std::string& host, uint16_t port)>;
  static std::vector<TcpAddress> systemLookup(const std::string& host, uint16_t port);

  explicit TcpClients(Lookup lookup = systemLookup);
  ~TcpClients();
  TcpClients(const TcpClients&) = delete;
  TcpClients& operator=(const TcpClients&) = delete;

  uint32_t connect(const std::string& app, const std::string& host, uint16_t port, int timeoutMs);
  bool send(const std::string& app, uint32_t id, const std::string& data);
  void close(const std::string& app, uint32_t id);
  void forget(const std::string& app);
  bool pop(TcpEvent& out);

 private:
  enum class State { Resolve, Connecting, Open, Closed };
  struct Conn {
    std::string app, host;
    uint16_t port = 0;
    int fd = -1;
    State state = State::Resolve;
    std::shared_ptr<posix::Resolver> lookup;
    int64_t deadline = 0;
    std::string in, out;
    bool dropped = false;
    std::vector<TcpAddress> addresses;
    std::size_t nextAddress = 0;
    int lastError = 0;
    std::deque<TcpEvent> events;
    std::size_t queued = 0;
  };
  void run();
  void wake();
  void startLookups();
  void takeAnswers();
  void expire(int64_t now);
  void connectNext(uint32_t id, Conn& c);
  void finish(uint32_t id, Conn& c, const char* reason);
  void emit(Conn& c, uint32_t id, TcpEvent::Kind kind, std::string data);
  void readFrom(uint32_t id, Conn& c);
  void writeTo(uint32_t id, Conn& c);
  void reap();

  Lookup lookup_;
  std::mutex mutex_;
  std::map<uint32_t, Conn> conns_;
  uint32_t next_ = 1;
  uint32_t cursor_ = 0;
  int wake_ = -1;
  bool quit_ = false;
  std::thread worker_;
};

}
