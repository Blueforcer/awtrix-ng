#include "platform/host/Wake.h"

namespace awtrix::os {
namespace {

Socket loopbackUdp(sockaddr_in& address) {
  if (!startSockets()) return kNoSocket;
  const Socket socket = ::WSASocketW(AF_INET, SOCK_DGRAM, 0, nullptr, 0, WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
  if (socket == INVALID_SOCKET) return kNoSocket;
  u_long on = 1;
  address = {};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  int size = sizeof address;
  if (::ioctlsocket(socket, FIONBIO, &on) != 0 ||
      ::bind(socket, reinterpret_cast<const sockaddr*>(&address), sizeof address) != 0 ||
      ::getsockname(socket, reinterpret_cast<sockaddr*>(&address), &size) != 0) {
    ::closesocket(socket);
    return kNoSocket;
  }
  return socket;
}

}

Wake::Wake() {
  sockaddr_in reader{}, writer{};
  const Socket in = loopbackUdp(reader);
  if (in == kNoSocket) return;
  const Socket out = loopbackUdp(writer);
  if (out == kNoSocket ||
      ::connect(out, reinterpret_cast<const sockaddr*>(&reader), sizeof reader) != 0 ||
      ::connect(in, reinterpret_cast<const sockaddr*>(&writer), sizeof writer) != 0) {
    ::closesocket(in);
    if (out != kNoSocket) ::closesocket(out);
    return;
  }
  read_ = in;
  write_ = out;
}

Wake::~Wake() {
  if (read_ != kNoSocket) ::closesocket(read_);
  if (write_ != kNoSocket) ::closesocket(write_);
}

void Wake::signal() {
  if (write_ == kNoSocket) return;
  const char one = 1;
  ::send(write_, &one, 1, 0);
}

void Wake::drain() {
  if (read_ == kNoSocket) return;
  char bytes[64];
  while (::recv(read_, bytes, sizeof bytes, 0) > 0) {
  }
}

}
