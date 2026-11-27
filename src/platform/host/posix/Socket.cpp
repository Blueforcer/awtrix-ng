#include "platform/host/Socket.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <initializer_list>

namespace awtrix::os {
namespace {

#if defined(MSG_NOSIGNAL)
constexpr int kNoSignal = MSG_NOSIGNAL;
#else
constexpr int kNoSignal = 0;
#endif

Socket open(int family, int type) {
#if defined(SOCK_NONBLOCK) && defined(SOCK_CLOEXEC)
  const Socket socket = ::socket(family, type | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (socket < 0) return kNoSocket;
#else
  const Socket socket = ::socket(family, type, 0);
  if (socket < 0) return kNoSocket;
  const int flags = ::fcntl(socket, F_GETFL);
  if (flags < 0 || ::fcntl(socket, F_SETFL, flags | O_NONBLOCK) < 0 || ::fcntl(socket, F_SETFD, FD_CLOEXEC) < 0) {
    ::close(socket);
    return kNoSocket;
  }
#endif
#if defined(SO_NOSIGPIPE)
  const int on = 1;
  ::setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
#endif
  return socket;
}

bool bindAny(Socket socket, uint16_t port) {
  sockaddr_in local{};
  local.sin_family = AF_INET;
  local.sin_addr.s_addr = htonl(INADDR_ANY);
  local.sin_port = htons(port);
  return ::bind(socket, reinterpret_cast<sockaddr*>(&local), sizeof local) == 0;
}

}

bool startSockets() { return true; }

Socket openStream(int family) { return open(family, SOCK_STREAM); }

Socket openUdp(uint16_t port, bool broadcast) {
  const Socket socket = open(AF_INET, SOCK_DGRAM);
  if (socket == kNoSocket) return kNoSocket;
  const int on = 1;
  ::setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
  if (broadcast) ::setsockopt(socket, SOL_SOCKET, SO_BROADCAST, &on, sizeof on);
  if (bindAny(socket, port)) return socket;
  ::close(socket);
  return kNoSocket;
}

void closeSocket(Socket socket) {
  if (socket >= 0) ::close(socket);
}

void shutdownSocket(Socket socket) {
  if (socket >= 0) ::shutdown(socket, SHUT_RDWR);
}

Socket duplicateSocket(Socket socket) {
  const int copy = ::fcntl(socket, F_DUPFD_CLOEXEC, 0);
  return copy < 0 ? kNoSocket : copy;
}

bool noDelay(Socket socket) {
  const int one = 1;
  return ::setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one) == 0;
}

int connectSocket(Socket socket, const sockaddr* address, socklen_t size) {
  return ::connect(socket, address, size) == 0 ? 0 : errno;
}

int socketError() { return errno; }

bool wouldBlock(int error) { return error == EAGAIN || error == EWOULDBLOCK; }

bool connecting(int error) { return error == EINPROGRESS; }

bool interrupted(int error) { return error == EINTR; }

SocketFailure failureOf(int error) {
  if (error == ECONNREFUSED) return SocketFailure::Refused;
  if (error == ETIMEDOUT) return SocketFailure::TimedOut;
  return SocketFailure::Other;
}

long sendBytes(Socket socket, const void* data, std::size_t size) { return ::send(socket, data, size, kNoSignal); }

long receiveBytes(Socket socket, void* data, std::size_t size, bool peek) {
  return ::recv(socket, data, size, peek ? MSG_PEEK : 0);
}

long sendDatagram(Socket socket, const void* data, std::size_t size, const sockaddr_in& to) {
  return ::sendto(socket, data, size, kNoSignal, reinterpret_cast<const sockaddr*>(&to), sizeof to);
}

long receiveDatagram(Socket socket, void* data, std::size_t size, sockaddr_in& from) {
  socklen_t length = sizeof from;
  return ::recvfrom(socket, data, size, 0, reinterpret_cast<sockaddr*>(&from), &length);
}

int pendingBytes(Socket socket) {
  int count = 0;
  return ::ioctl(socket, FIONREAD, &count) == 0 ? count : 0;
}

int pendingError(Socket socket) {
  int error = 0;
  socklen_t size = sizeof error;
  return ::getsockopt(socket, SOL_SOCKET, SO_ERROR, &error, &size) == 0 ? error : errno;
}

int pollSockets(PollEntry* entries, std::size_t count, int timeoutMs) {
  return ::poll(entries, static_cast<nfds_t>(count), timeoutMs);
}

bool waitWritable(Socket socket, int timeoutMs) {
  pollfd entry{socket, POLLOUT, 0};
  return ::poll(&entry, 1, timeoutMs) > 0 && (entry.revents & POLLOUT);
}

bool parseIp(int family, const char* text, void* address) { return ::inet_pton(family, text, address) == 1; }

bool localPortFree(uint16_t port) {
  for (const int type : {SOCK_STREAM, SOCK_DGRAM}) {
    const Socket socket = open(AF_INET, type);
    if (socket == kNoSocket) return false;
    const bool free = bindAny(socket, port);
    ::close(socket);
    if (!free) return false;
  }
  return true;
}

}
