#include "platform/host/Socket.h"

#include <mswsock.h>

#include <climits>
#include <initializer_list>
#include <mutex>

namespace awtrix::os {
namespace {

constexpr int kMaxChunk = INT_MAX;

int chunk(std::size_t size) { return size > static_cast<std::size_t>(kMaxChunk) ? kMaxChunk : static_cast<int>(size); }

Socket open(int family, int type) {
  const Socket socket = ::WSASocketW(family, type, 0, nullptr, 0, WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
  if (socket == INVALID_SOCKET) return kNoSocket;
  u_long on = 1;
  if (::ioctlsocket(socket, FIONBIO, &on) != 0) {
    ::closesocket(socket);
    return kNoSocket;
  }
  return socket;
}

bool bindAny(Socket socket, uint16_t port) {
  sockaddr_in local{};
  local.sin_family = AF_INET;
  local.sin_addr.s_addr = htonl(INADDR_ANY);
  local.sin_port = htons(port);
  return ::bind(socket, reinterpret_cast<sockaddr*>(&local), sizeof local) == 0;
}

void ignoreUnreachable(Socket socket) {
  BOOL off = FALSE;
  DWORD returned = 0;
  ::WSAIoctl(socket, SIO_UDP_CONNRESET, &off, sizeof off, nullptr, 0, &returned, nullptr, nullptr);
}

short wsaEvents(short events) {
  short out = 0;
  if (events & POLLIN) out |= POLLRDNORM;
  if (events & POLLOUT) out |= POLLWRNORM;
  return out;
}

}

bool startSockets() {
  static std::once_flag once;
  static bool started = false;
  std::call_once(once, [] {
    WSADATA data{};
    started = ::WSAStartup(MAKEWORD(2, 2), &data) == 0;
  });
  return started;
}

Socket openStream(int family) { return open(family, SOCK_STREAM); }

Socket openUdp(uint16_t port, bool broadcast) {
  const Socket socket = open(AF_INET, SOCK_DGRAM);
  if (socket == kNoSocket) return kNoSocket;
  ignoreUnreachable(socket);
  if (broadcast) {
    const BOOL on = TRUE;
    ::setsockopt(socket, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&on), sizeof on);
  }
  if (bindAny(socket, port)) return socket;
  ::closesocket(socket);
  return kNoSocket;
}

void closeSocket(Socket socket) {
  if (socket != kNoSocket) ::closesocket(socket);
}

void shutdownSocket(Socket socket) {
  if (socket != kNoSocket) ::shutdown(socket, SD_BOTH);
}

Socket duplicateSocket(Socket socket) {
  WSAPROTOCOL_INFOW info{};
  if (::WSADuplicateSocketW(socket, ::GetCurrentProcessId(), &info) != 0) return kNoSocket;
  const Socket copy = ::WSASocketW(FROM_PROTOCOL_INFO, FROM_PROTOCOL_INFO, FROM_PROTOCOL_INFO, &info, 0,
                                   WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
  return copy == INVALID_SOCKET ? kNoSocket : copy;
}

bool noDelay(Socket socket) {
  const BOOL one = TRUE;
  return ::setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof one) == 0;
}

void exclusiveListener(Socket socket) {
  const BOOL one = TRUE;
  ::setsockopt(socket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&one), sizeof one);
}

int connectSocket(Socket socket, const sockaddr* address, socklen_t size) {
  return ::connect(socket, address, size) == 0 ? 0 : ::WSAGetLastError();
}

int socketError() { return ::WSAGetLastError(); }

bool wouldBlock(int error) { return error == WSAEWOULDBLOCK; }

bool connecting(int error) { return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS; }

bool interrupted(int error) { return error == WSAEINTR; }

SocketFailure failureOf(int error) {
  if (error == WSAECONNREFUSED) return SocketFailure::Refused;
  if (error == WSAETIMEDOUT) return SocketFailure::TimedOut;
  return SocketFailure::Other;
}

long sendBytes(Socket socket, const void* data, std::size_t size) {
  return ::send(socket, static_cast<const char*>(data), chunk(size), 0);
}

long receiveBytes(Socket socket, void* data, std::size_t size, bool peek) {
  return ::recv(socket, static_cast<char*>(data), chunk(size), peek ? MSG_PEEK : 0);
}

long sendDatagram(Socket socket, const void* data, std::size_t size, const sockaddr_in& to) {
  return ::sendto(socket, static_cast<const char*>(data), chunk(size), 0, reinterpret_cast<const sockaddr*>(&to),
                  sizeof to);
}

long receiveDatagram(Socket socket, void* data, std::size_t size, sockaddr_in& from) {
  int length = sizeof from;
  const int count =
      ::recvfrom(socket, static_cast<char*>(data), chunk(size), 0, reinterpret_cast<sockaddr*>(&from), &length);
  if (count == SOCKET_ERROR && ::WSAGetLastError() == WSAEMSGSIZE) return chunk(size);
  return count;
}

int pendingBytes(Socket socket) {
  u_long count = 0;
  return ::ioctlsocket(socket, FIONREAD, &count) == 0 ? static_cast<int>(count > INT_MAX ? INT_MAX : count) : 0;
}

int pendingError(Socket socket) {
  int error = 0;
  int size = sizeof error;
  return ::getsockopt(socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &size) == 0 ? error
                                                                                               : ::WSAGetLastError();
}

int pollSockets(PollEntry* entries, std::size_t count, int timeoutMs) {
  if (count == 0) {
    ::Sleep(timeoutMs < 0 ? INFINITE : static_cast<DWORD>(timeoutMs));
    return 0;
  }
  for (std::size_t i = 0; i < count; ++i) {
    entries[i].events = wsaEvents(entries[i].events);
    entries[i].revents = 0;
  }
  return ::WSAPoll(entries, static_cast<ULONG>(count), timeoutMs);
}

bool waitWritable(Socket socket, int timeoutMs) {
  WSAPOLLFD entry{socket, POLLWRNORM, 0};
  return ::WSAPoll(&entry, 1, timeoutMs) > 0 && (entry.revents & POLLWRNORM);
}

bool parseIp(int family, const char* text, void* address) { return ::inet_pton(family, text, address) == 1; }

bool localPortFree(uint16_t port) {
  for (const int type : {SOCK_STREAM, SOCK_DGRAM}) {
    const Socket socket = open(AF_INET, type);
    if (socket == kNoSocket) return false;
    const bool free = bindAny(socket, port);
    ::closesocket(socket);
    if (!free) return false;
  }
  return true;
}

}
