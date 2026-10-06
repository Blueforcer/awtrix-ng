#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#include "platform/linux/tls/TlsTrust.h"
#include "platform/tc002/voice/WebSocket.h"
using awtrix::tc002::voice::WebSocket;
int main(int argc, char** argv) {
  if (argc != 6) return 2;
  const std::string trust = argv[4], mode = argv[5];
  std::string error;
  if (!trust.empty() && !awtrix::loadTlsTrust(trust, error)) return 7;
  const bool refused = mode == "reject" || mode == "notrust" || mode == "badaccept";
  const bool failing = refused || mode == "oversize" || mode == "binary" || mode == "masked";
  WebSocket socket;
  auto generation = socket.connect(
      {argv[1], argv[2], "/api/websocket", std::string(argv[3]) == "tls"});
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
  bool sent = false, received = false;
  while (std::chrono::steady_clock::now() < deadline) {
    for (const auto& e : socket.poll()) {
      if (e.generation != generation) return 3;
      if (e.kind == WebSocket::Event::Open) {
        if (refused) return 4;
        if (mode == "echo")
          sent = socket.send(std::string("\x07\x00\xff", 3), true);
      }
      if (e.kind == WebSocket::Event::Text && e.text == "exact") {
        if (mode == "echo" && sent) {
          socket.cancel();
          return 0;
        }
        received = mode == "fragments";
      }
      if (e.kind == WebSocket::Event::Closed) return received ? 0 : 8;
      if (e.kind == WebSocket::Event::Failed) return failing ? 0 : 5;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  std::fputs("websocket deadline exceeded\n", stderr);
  return 6;
}
