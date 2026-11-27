#pragma once

#include "platform/host/Socket.h"

namespace awtrix::test {

inline int freePort(int type) {
  if (!os::startSockets()) return 0;
  const os::Socket socket = ::socket(AF_INET, type, 0);
  if (socket == os::kNoSocket) return 0;
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  socklen_t size = sizeof(address);
  const bool okay = ::bind(socket, reinterpret_cast<sockaddr*>(&address), size) == 0 &&
                    ::getsockname(socket, reinterpret_cast<sockaddr*>(&address), &size) == 0;
  os::closeSocket(socket);
  return okay ? ntohs(address.sin_port) : 0;
}

inline void receiveTimeout(os::Socket socket, int milliseconds) {
#if defined(_WIN32)
  const DWORD timeout = static_cast<DWORD>(milliseconds);
#else
  const timeval timeout{milliseconds / 1000, (milliseconds % 1000) * 1000};
#endif
  ::setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof timeout);
}

}
