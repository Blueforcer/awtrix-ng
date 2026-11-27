#pragma once

#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#endif

namespace awtrix::os {

#if defined(_WIN32)
using Socket = SOCKET;
inline constexpr Socket kNoSocket = INVALID_SOCKET;
using PollEntry = WSAPOLLFD;
#else
using Socket = int;
inline constexpr Socket kNoSocket = -1;
using PollEntry = pollfd;
#endif

enum class SocketFailure { Refused, TimedOut, Other };

bool startSockets();
Socket openStream(int family);
Socket openUdp(uint16_t port, bool broadcast = false);
void closeSocket(Socket socket);
void shutdownSocket(Socket socket);
Socket duplicateSocket(Socket socket);
bool noDelay(Socket socket);
int connectSocket(Socket socket, const sockaddr* address, socklen_t size);

int socketError();
bool wouldBlock(int error);
bool connecting(int error);
bool interrupted(int error);
SocketFailure failureOf(int error);

long sendBytes(Socket socket, const void* data, std::size_t size);
long receiveBytes(Socket socket, void* data, std::size_t size, bool peek = false);
long sendDatagram(Socket socket, const void* data, std::size_t size, const sockaddr_in& to);
long receiveDatagram(Socket socket, void* data, std::size_t size, sockaddr_in& from);
int pendingBytes(Socket socket);
int pendingError(Socket socket);

int pollSockets(PollEntry* entries, std::size_t count, int timeoutMs);
bool waitWritable(Socket socket, int timeoutMs);
bool localPortFree(uint16_t port);
bool parseIp(int family, const char* text, void* address);

class UniqueSocket {
 public:
  UniqueSocket() = default;
  explicit UniqueSocket(Socket socket) : socket_(socket) {}
  ~UniqueSocket() { reset(); }
  UniqueSocket(UniqueSocket&& other) noexcept : socket_(other.release()) {}
  UniqueSocket& operator=(UniqueSocket&& other) noexcept {
    if (this != &other) reset(other.release());
    return *this;
  }
  UniqueSocket(const UniqueSocket&) = delete;
  UniqueSocket& operator=(const UniqueSocket&) = delete;

  Socket get() const { return socket_; }
  bool valid() const { return socket_ != kNoSocket; }
  Socket release() {
    const Socket socket = socket_;
    socket_ = kNoSocket;
    return socket;
  }
  void reset(Socket socket = kNoSocket) {
    if (socket_ != kNoSocket) closeSocket(socket_);
    socket_ = socket;
  }

 private:
  Socket socket_ = kNoSocket;
};

}
