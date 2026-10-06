#include "../../test/EngineFakes.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>

#include "core/CoreEngine.h"
#include "core/apps/AppRegistry.h"
#include "core/render/Canvas.h"
#include "core/script/ScriptHost.h"
#include "core/script/ScriptService.h"
#include "persistence/DeviceConfig.h"
#include "platform/linux/host/HostHttpServer.h"
#include "platform/linux/host/vendor/httplib.h"

using namespace awtrix;

namespace {

using Display = awtrix::test::NullDisplay;
using System = awtrix::test::NullSystem;

int availablePort() {
  const int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) return 0;
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  socklen_t size = sizeof(address);
  const bool okay = ::bind(fd, reinterpret_cast<sockaddr*>(&address), size) == 0 &&
      ::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &size) == 0;
  ::close(fd);
  return okay ? ntohs(address.sin_port) : 0;
}

bool rejectsHeaders(int port, std::size_t bytes) {
  const int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) return false;
  timeval timeout{2, 0};
  ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(static_cast<uint16_t>(port));
  const std::string request = "GET /api/v1/version HTTP/1.1\r\nHost: 127.0.0.1:" +
      std::to_string(port) + "\r\nContent-Length: " + std::to_string(bytes) +
      "\r\nConnection: close\r\n\r\n";
  char reply[1024]{};
  const bool sent = ::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0 &&
      ::send(fd, request.data(), request.size(), MSG_NOSIGNAL) == static_cast<ssize_t>(request.size());
  const ssize_t received = sent ? ::recv(fd, reply, sizeof(reply), 0) : -1;
  ::close(fd);
  return received > 0 && std::string(reply, static_cast<std::size_t>(received)).find("HTTP/1.1 413 ") == 0;
}

// Berry source whose comments are full of what a form parser would act on.
std::string formLikeSource(std::size_t bytes) {
  std::string source = "# @name Form\nclass App\n  def draw() end\nend\n";
  while (source.size() < bytes) source += "# a=b&c=d%20e+f&secrets=1\n";
  return source + "return App()\n";
}

}

int main() {
  sound::AudioRouter audio;
  Display display;
  System system;
  CoreEngine engine(audio, display, system);
  Canvas canvas(52, 16);
  DeviceConfig config;
  HostHttpOptions options;
  options.maxBodyBytes = 2u << 20;
  options.updateUploadBytes = 8u << 20;
  options.updateUpload = [](const auto&, auto&, const auto&) { throw std::runtime_error("fixture detail"); };
  std::map<std::string, std::string> saved;
  script::ScriptServices services;
  services.monotonicMs = [] { return int64_t{0}; };
  services.readSource = [&](const std::string& name, std::string& out) {
    const auto it = saved.find(name);
    if (it == saved.end()) return false;
    out = it->second;
    return true;
  };
  AppRegistry registry;
  script::ScriptHost scripts(registry, services, nullptr, nullptr);
  script::ScriptService scriptService(
      scripts, [&](const std::string& name, const std::string& source) { saved[name] = source; }, nullptr);
  engine.setScriptService(&scriptService);
  const int port = availablePort();
  HostHttpServer server;
  if (!port || !server.begin(static_cast<uint16_t>(port), engine, canvas, "fixture", config, "", options)) return 1;
  std::atomic<bool> done{false};
  int failures = 0;
  std::thread client([&] {
    for (std::size_t bytes : {std::size_t(65537), std::size_t(1u << 20), std::size_t(3u << 20)}) {
      if (!rejectsHeaders(port, bytes)) {
        std::printf("FAIL: GET body %zu was not refused before reading\n", bytes);
        ++failures;
      }
    }
    httplib::Client http("127.0.0.1", port);
    const auto response = http.Get("/api/v1/version");
    if (!response || response->status != 200) ++failures;
    const auto failed = http.Post("/update", "fixture", "application/octet-stream");
    if (!failed || failed->status != 500 || failed->has_header("EXCEPTION_WHAT") ||
        failed->body.find("fixture detail") != std::string::npos) {
      std::printf("FAIL: handler exception details escaped the server\n");
      ++failures;
    }
    // curl's default for --data-binary: the source must reach the script whatever it is labelled.
    for (std::size_t bytes : {std::size_t(300), std::size_t(20000)}) {
      const std::string source = formLikeSource(bytes);
      const auto put = http.Put("/api/v1/apps/script/Form", source, "application/x-www-form-urlencoded");
      if (!put || put->status != 200 || put->body.find("\"error\":null") == std::string::npos) {
        std::printf("FAIL: form-encoded script upload of %zu bytes answered %d %s\n", source.size(),
                    put ? put->status : -1, put ? put->body.c_str() : "");
        ++failures;
      } else if (saved["Form"] != source) {
        std::printf("FAIL: form-encoded script upload of %zu bytes was not stored as sent\n", source.size());
        ++failures;
      }
    }
    done = true;
  });
  while (!done.load()) {
    server.tick();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  client.join();
  server.stop();
  std::printf("http body limit: %d failure(s)\n", failures);
  return failures ? 1 : 0;
}
