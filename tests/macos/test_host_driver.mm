#include "../support.h"
#include "../Sockets.h"

#import <Foundation/Foundation.h>

#include <signal.h>

#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "platform/host/app/StatusFetch.h"
#include "platform/host/app/macos/HostDriver.h"
#include "platform/linux/hostd/Contract.h"

namespace fs = std::filesystem;
namespace os = awtrix::os;
namespace hostd = awtrix::hostd;
using awtrix::hostapp::HostDriver;
using awtrix::hostapp::HostState;
using awtrix::hostapp::HostSupervisor;
using awtrix::hostapp::StopReason;
using awtrix::test::check;
using awtrix::test::require;

namespace {

std::string g_host;
std::string g_runner;
std::string g_webui;
fs::path g_root;

struct Watch {
  int changes = 0;
  bool offMain = false;
};

bool pump(const std::function<bool()>& done, int ms) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
  while (!done() && std::chrono::steady_clock::now() < deadline)
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.02, true);
  return done();
}

void idle(int ms) {
  pump([] { return false; }, ms);
}

bool answers(int port) {
  std::string body;
  return awtrix::hostapp::fetchStatus(port, body);
}

bool alive(long pid) { return pid > 0 && ::kill(static_cast<pid_t>(pid), 0) == 0; }

std::vector<std::string> hostArgs(const std::string& name, int port) {
  return {g_host,      "--state", (g_root / name).string(), "--log", (g_root / (name + ".log")).string(),
          "--exit-with-stdin", "--binary", g_runner, "--webui", g_webui, "--port", std::to_string(port),
          "--no-discovery", "--name", "test"};
}

HostDriver::Changed watched(Watch& watch) {
  return [&watch] {
    ++watch.changes;
    if (![NSThread isMainThread]) watch.offMain = true;
  };
}

void runsAndQuits() {
  const int port = awtrix::test::freePort(SOCK_STREAM);
  Watch watch;
  HostDriver driver(hostArgs("runs", port), dispatch_get_main_queue(), watched(watch));
  check(driver.view().state == HostState::Stopped && driver.processId() < 0, "nothing runs before start");
  driver.start();
  check(driver.view().state == HostState::Starting && alive(driver.processId()) && watch.changes == 1,
        "start launches awtrix-host and reports the change");
  check(pump([&] { return driver.view().state == HostState::Running; }, HostSupervisor::kSettleMs + 3000),
        "the wake-up turns it to running without anyone asking");
  check(watch.changes == 2 && !watch.offMain, "that change is reported too, on the main queue");
  check(pump([&] { return answers(port); }, 15000), "the host answers its status");

  const long pid = driver.processId();
  driver.quit();
  check(driver.view().state == HostState::Stopped && driver.view().reason == StopReason::Quit,
        "quit stops it for good");
  check(!alive(pid) && !answers(port), "the host is gone and its port closed");
  const int changes = watch.changes;
  idle(1000);
  check(watch.changes == changes && driver.processId() < 0, "and nothing happens after it");
}

void restartsAfterACrash() {
  const int port = awtrix::test::freePort(SOCK_STREAM);
  Watch watch;
  HostDriver driver(hostArgs("crash", port), dispatch_get_main_queue(), watched(watch));
  driver.start();
  require(pump([&] { return answers(port); }, 15000), "a host that answers");
  const long first = driver.processId();
  ::kill(static_cast<pid_t>(first), SIGKILL);
  check(pump([&] { return driver.view().state == HostState::Restarting; }, 3000),
        "a host killed from outside is noticed at once");
  const int64_t left = driver.view().restartInMs;
  check(left > HostSupervisor::kBackoffMs[0] - 3000 && left <= HostSupervisor::kBackoffMs[0],
        "and restarted after the first back-off");
  check(pump([&] { return driver.view().state == HostState::Starting; }, HostSupervisor::kBackoffMs[0] + 3000),
        "the wake-up starts it again");
  check(alive(driver.processId()) && driver.processId() != first, "as a new process");
  check(pump([&] { return answers(port); }, 15000), "that answers");
  driver.quit();
  check(!watch.offMain, "every change came on the main queue");
}

void stopsWhenThePortIsTaken() {
  const int port = awtrix::test::freePort(SOCK_STREAM);
  Watch watch;
  HostDriver first(hostArgs("first", port), dispatch_get_main_queue(), watched(watch));
  first.start();
  require(pump([&] { return answers(port); }, 15000), "a first host");

  Watch secondWatch;
  HostDriver second(hostArgs("second", port), dispatch_get_main_queue(), watched(secondWatch));
  second.start();
  check(pump([&] { return second.view().state == HostState::Stopped; }, 15000) &&
            second.view().reason == StopReason::PortBusy,
        "a second host on the same port stops with the port-busy reason");
  const int changes = secondWatch.changes;
  idle(HostSupervisor::kSettleMs + 500);
  check(second.view().state == HostState::Stopped && second.processId() < 0 && secondWatch.changes == changes,
        "and does not try again by itself");
  check(answers(port) && first.view().up(), "the first keeps running");

  first.quit();
  require(pump([&] { return !answers(port); }, 5000), "the port is free again");
  second.start();
  check(second.view().state == HostState::Starting && pump([&] { return answers(port); }, 15000),
        "start again runs the second host on the freed port");
  second.quit();
}

void cannotStart() {
  Watch watch;
  std::vector<std::string> args = hostArgs("missing", awtrix::test::freePort(SOCK_STREAM));
  args[0] = (g_root / "no-such-host").string();
  HostDriver driver(args, dispatch_get_main_queue(), watched(watch));
  driver.start();
  check(driver.view().state == HostState::Stopped && driver.view().reason == StopReason::CannotStart &&
            watch.changes == 1,
        "a host program that is missing stops at once with its own reason");
  idle(HostSupervisor::kSettleMs + 500);
  check(watch.changes == 1, "and nothing is planned");
}

void quitCancelsTheRestart() {
  Watch watch;
  HostDriver driver({g_host, "--port"}, dispatch_get_main_queue(), watched(watch));
  driver.start();
  check(pump([&] { return driver.view().state == HostState::Restarting; }, 3000),
        "a host that ends right after its start is noticed");
  driver.quit();
  const int changes = watch.changes;
  idle(static_cast<int>(HostSupervisor::kBackoffMs[0]) + 1000);
  check(driver.view().state == HostState::Stopped && driver.view().reason == StopReason::Quit &&
            driver.processId() < 0 && watch.changes == changes,
        "quit while it waits to restart cancels the restart");
}

}

int main(int argc, char** argv) {
  require(argc == 4, "usage: macos-host-driver-test AWTRIX_HOST AWTRIX_SCRIPT_RUNNER WEBUI");
  g_host = argv[1];
  g_runner = argv[2];
  g_webui = argv[3];
  g_root = fs::temp_directory_path() /
           ("awtrix-macos-driver-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  fs::create_directories(g_root);
  @autoreleasepool {
    runsAndQuits();
    restartsAfterACrash();
    stopsWhenThePortIsTaken();
    cannotStart();
    quitCancelsTheRestart();
  }
  std::error_code ignored;
  fs::remove_all(g_root, ignored);
  return awtrix::test::finish("macOS host driver");
}
