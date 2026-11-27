#include "../support.h"
#include "../Sockets.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "core/api/JsonReader.h"
#include "platform/host/Process.h"
#include "platform/host/app/StatusFetch.h"
#include "platform/host/app/StatusMenu.h"
#include "platform/linux/host/vendor/httplib.h"
#include "platform/linux/hostd/Contract.h"

namespace fs = std::filesystem;
namespace os = awtrix::os;
namespace api = awtrix::api;
namespace hostapp = awtrix::hostapp;
namespace hostd = awtrix::hostd;
using awtrix::test::check;
using awtrix::test::require;

namespace {

std::string g_host;
std::string g_runner;
std::string g_webui;
fs::path g_root;

bool waitFor(const std::function<bool()>& done, int ms) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
  while (std::chrono::steady_clock::now() < deadline) {
    if (done()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return done();
}

bool answers(int port, const char* path = "/api/v1/status") {
  httplib::Client client("127.0.0.1", port);
  client.set_connection_timeout(0, 300000);
  client.set_read_timeout(2, 0);
  return static_cast<bool>(client.Get(path));
}

std::string slurp(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), {});
}

std::vector<std::string> hostArgs(const std::string& state, int port, std::vector<std::string> extra) {
  std::vector<std::string> args = {g_host, "--state", (g_root / state).u8string(), "--port", std::to_string(port),
                                   "--binary", g_runner, "--webui", g_webui, "--no-discovery", "--name", "test"};
  args.insert(args.end(), extra.begin(), extra.end());
  return args;
}

int freePort() { return awtrix::test::freePort(SOCK_STREAM); }

bool expect(bool condition, std::string_view message) {
  check(condition, message);
  return condition;
}

void endsWithItsInput() {
  const int port = freePort();
  os::TiedProcess host;
  if (!expect(host.start(hostArgs("tied", port, {"--exit-with-stdin", "--idle", "0"})),
              "the host starts with a pipe as its input"))
    return;
  check(host.running() && host.id() > 0, "it runs with an id");
  if (!expect(waitFor([&] { return answers(port); }, 15000), "and answers")) return;

  std::string status;
  if (!expect(hostapp::fetchStatus(port, status), "fetchStatus reads its status")) return;
  const std::string code = api::memberText(status, "code");
  check(code.size() == 6, "with the pairing code, asked from this computer");

  httplib::Client client("127.0.0.1", port);
  client.set_read_timeout(20, 0);
  const std::string pairing = R"({"uid":"c0ffee","name":"desk","width":32,"height":8,"board":"awtrixng",)"
                              R"("clock":"http://127.0.0.1:9","code":")" + code + R"("})";
  const httplib::Result paired = client.Post("/api/v1/pair", pairing, "application/json");
  if (!expect(paired && paired->status == 200, "a clock pairs")) return;
  long long runnerPort = 0;
  api::memberValue(paired->body, "port").asLong(runnerPort);
  const int runner = static_cast<int>(runnerPort);
  if (!expect(runner > 0 && waitFor([&] { return answers(runner, "/api/v1/version"); }, 15000),
              "its runner answers"))
    return;

  if (!expect(hostapp::fetchStatus(port, status), "the status after pairing")) return;
  hostapp::MenuInput menu;
  menu.port = port;
  menu.host.state = hostapp::HostState::Running;
  menu.status = status;
  const std::vector<hostapp::MenuItem> items = hostapp::buildStatusMenu(menu);
  check(std::any_of(items.begin(), items.end(),
                    [](const hostapp::MenuItem& item) {
                      return item.text == "desk \u00b7 127.0.0.1:9 \u00b7 running" &&
                             item.action == hostapp::MenuAction::OpenClock && item.value == "http://127.0.0.1:9";
                    }),
        "the menu lists the paired clock from the real status, opening its web UI");
  check(std::any_of(items.begin(), items.end(),
                    [](const hostapp::MenuItem& item) { return item.action == hostapp::MenuAction::CopyCode; }),
        "and the code to copy");

  os::TiedProcess second;
  int exitCode = -1;
  check(second.start(hostArgs("second", port, {"--exit-with-stdin"})) && second.wait(15000, exitCode) &&
            exitCode == hostd::kExitPortBusy,
        "a second host on the same port ends with the port-busy code, though the first is a host itself");
  check(answers(port), "the first keeps answering");

  host.closeInput();
  check(host.wait(15000, exitCode) && exitCode == 0, "closing its input ends the host cleanly");
  check(!host.running() && !answers(port), "its port is closed");
  check(!answers(runner, "/api/v1/version"), "and its runner is gone with it");
}

void runsOnWithoutTheSwitch() {
  const int port = freePort();
  os::TiedProcess host;
  if (!expect(host.start(hostArgs("plain", port, {})), "a host without --exit-with-stdin starts")) return;
  host.closeInput();
  check(waitFor([&] { return answers(port); }, 15000), "it answers with its input already closed");
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  int status = -1;
  check(!host.ended(status) && answers(port), "and keeps running");
  host.stop(100);
  check(!host.running() && !answers(port), "stop ends it once the grace time is up");
}

bool runOnce(const std::vector<std::string>& args, int port) {
  os::TiedProcess host;
  if (!host.start(args) || !waitFor([&] { return answers(port); }, 15000)) return false;
  host.closeInput();
  int status = -1;
  return host.wait(15000, status) && status == 0;
}

void logs() {
  const int port = freePort();
  const fs::path folder = g_root / "logs" / "nested";
  const fs::path log = folder / "awtrix-host.log";
  const fs::path older = folder / "awtrix-host.log.1";
  const std::vector<std::string> args = hostArgs("logged", port, {"--exit-with-stdin", "--log", log.u8string()});
  const std::string startLine = "on port " + std::to_string(port);

  check(runOnce(args, port), "a host with --log in a folder that does not exist yet runs");
  check(slurp(log).find(startLine) != std::string::npos, "and writes its log there");
  check(!fs::exists(older), "with nothing to move aside");

  const std::string full(hostd::kLogKeepBytes, 'x');
  std::ofstream(log, std::ios::binary | std::ios::trunc) << full;
  check(runOnce(args, port), "a host whose log is exactly at the limit runs");
  const std::string appended = slurp(log);
  check(!fs::exists(older) && appended.size() > full.size() && appended.compare(0, full.size(), full) == 0 &&
            appended.find(startLine) != std::string::npos,
        "and appends to that log of exactly 1 MB");

  std::ofstream(log, std::ios::binary | std::ios::trunc) << full << 'y';
  check(runOnce(args, port), "a host whose log is one byte over runs");
  check(slurp(older) == full + "y", "and moves the log over 1 MB to awtrix-host.log.1");
  const std::string fresh = slurp(log);
  check(fresh.size() < 4096 && fresh.find(startLine) != std::string::npos, "and starts a new one");

  const fs::path blocked = g_root / "a-folder";
  fs::create_directories(blocked);
  check(runOnce(hostArgs("unlogged", port, {"--exit-with-stdin", "--log", blocked.u8string()}), port),
        "a host whose log cannot be written still runs and ends cleanly");
}

void portTaken() {
  const int port = freePort();
  require(os::startSockets(), "sockets");
  os::UniqueSocket held(::socket(AF_INET, SOCK_STREAM, 0));
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  address.sin_port = htons(static_cast<uint16_t>(port));
  require(held.valid() && ::bind(held.get(), reinterpret_cast<sockaddr*>(&address), sizeof address) == 0 &&
              ::listen(held.get(), 4) == 0,
          "another program listens on the port");
  os::TiedProcess host;
  int status = -1;
  check(host.start(hostArgs("busy", port, {"--exit-with-stdin"})) && host.wait(15000, status) &&
            status == hostd::kExitPortBusy,
        "a host whose port another program listens on ends with the port-busy code");

  os::TiedProcess wrong;
  check(wrong.start({g_host, "--port"}) && wrong.wait(15000, status) && status == hostd::kExitUsage,
        "a host with a broken command line ends with the usage code");

  os::TiedProcess missing;
  check(!missing.start({(g_root / "no-such-program").u8string()}) && !missing.running(),
        "a program that does not exist does not start");
  check(!missing.start({}), "nor does an empty command line");
}

}

int main(int argc, char** argv) {
  require(argc == 4, "usage: hostd-process-test AWTRIX_HOST AWTRIX_SCRIPT_RUNNER WEBUI");
  g_host = argv[1];
  g_runner = argv[2];
  g_webui = argv[3];
  g_root = fs::temp_directory_path() / ("awtrix-hostd-process-" + std::to_string(std::chrono::steady_clock::now()
                                                                                     .time_since_epoch()
                                                                                     .count()));
  fs::remove_all(g_root);
  fs::create_directories(g_root);
  endsWithItsInput();
  runsOnWithoutTheSwitch();
  logs();
  portTaken();
  std::error_code ignored;
  fs::remove_all(g_root, ignored);
  return awtrix::test::finish("awtrix-host as a tied process");
}
