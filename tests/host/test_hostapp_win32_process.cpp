#include "../support.h"
#include "../Sockets.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "platform/host/Socket.h"
#include "platform/host/app/StatusFetch.h"
#include "platform/host/app/win32/StartAtLogin.h"
#include "platform/host/app/win32/TiedHost.h"
#include "platform/host/app/win32/TrayController.h"
#include "platform/linux/host/vendor/httplib.h"
#include "platform/linux/hostd/Contract.h"

namespace fs = std::filesystem;
namespace os = awtrix::os;
namespace hostd = awtrix::hostd;
using namespace awtrix::hostapp;
using namespace awtrix::hostapp::win32;
using awtrix::test::check;
using awtrix::test::require;

namespace {

std::string g_host;
std::string g_runner;
std::string g_webui;
fs::path g_root;

int64_t nowMs() { return static_cast<int64_t>(::GetTickCount64()); }

bool waitFor(const std::function<bool()>& done, int ms) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
  while (std::chrono::steady_clock::now() < deadline) {
    if (done()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return done();
}

bool answers(int port) {
  httplib::Client client("127.0.0.1", port);
  client.set_connection_timeout(0, 300000);
  client.set_read_timeout(2, 0);
  return static_cast<bool>(client.Get("/api/v1/version"));
}

std::vector<std::string> hostArgs(const std::string& state, int port) {
  return {g_host,   "--state", (g_root / state).u8string(), "--port", std::to_string(port), "--binary",
          g_runner, "--webui", g_webui, "--no-discovery", "--name", "test", "--exit-with-stdin"};
}

class Signals {
 public:
  Signals() : event_(::CreateEventW(nullptr, FALSE, FALSE, nullptr)) { require(event_ != nullptr, "an event"); }
  ~Signals() { ::CloseHandle(event_); }
  std::function<void()> callback() {
    return [this] {
      ++count_;
      ::SetEvent(event_);
    };
  }
  bool await(int ms) { return ::WaitForSingleObject(event_, static_cast<DWORD>(ms)) == WAIT_OBJECT_0; }
  int count() const { return count_; }

 private:
  HANDLE event_;
  std::atomic<int> count_{0};
};

class Watched {
 public:
  explicit Watched(long id)
      : handle_(id > 0 ? ::OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(id))
                       : nullptr) {}
  ~Watched() {
    if (handle_) ::CloseHandle(handle_);
  }
  bool valid() const { return handle_ != nullptr; }
  bool gone(int ms = 0) const {
    return handle_ && ::WaitForSingleObject(handle_, static_cast<DWORD>(ms)) == WAIT_OBJECT_0;
  }
  std::optional<DWORD> exitCode() const {
    DWORD code = 0;
    if (!gone() || !::GetExitCodeProcess(handle_, &code)) return std::nullopt;
    return code;
  }

 private:
  HANDLE handle_;
};

class Journal {
 public:
  std::vector<std::string> lines;
};

class RecordingView : public TrayView {
 public:
  explicit RecordingView(Journal& journal) : journal_(journal) {}
  std::optional<HostState> state;
  std::string tip;
  int shows = 0;
  void show(HostState s, const std::string& t) override {
    ++shows;
    state = s;
    tip = t;
  }
  void wakeIn(std::optional<int64_t>) override {}
  void hide() override { journal_.lines.push_back("hide"); }

 private:
  Journal& journal_;
};

class NoShell : public Shell {
 public:
  bool open(const std::string&) override { return true; }
  bool copy(const std::string&) override { return true; }
  bool exists(const std::string&) override { return false; }
};

class MemoryRegistry : public LoginRegistry {
 public:
  std::optional<bool> choice() override { return std::nullopt; }
  bool remember(bool) override { return true; }
  std::optional<std::wstring> command() override { return std::nullopt; }
  bool setCommand(const std::wstring&) override { return true; }
  bool removeCommand() override { return true; }
  bool blockedByWindows() override { return false; }
  bool unblock() override { return true; }
};

class JournaledHost : public TiedHost {
 public:
  JournaledHost(std::function<void()> signal, Journal& journal) : TiedHost(std::move(signal)), journal_(journal) {}
  void stop() override {
    journal_.lines.push_back("stop");
    TiedHost::stop();
  }

 private:
  Journal& journal_;
};

struct Rig {
  explicit Rig(int port) : port(port) {}
  int port;
  Journal journal;
  Signals signals;
  JournaledHost host{signals.callback(), journal};
  RecordingView view{journal};
  NoShell shell;
  MemoryRegistry registry;
  StartAtLogin login{registry, L"\"x.exe\""};
};

std::optional<MenuItem> item(const std::vector<MenuItem>& items, MenuAction action) {
  const auto it = std::find_if(items.begin(), items.end(), [action](const MenuItem& i) { return i.action == action; });
  if (it == items.end()) return std::nullopt;
  return *it;
}

void aTakenPortStopsIt() {
  const int port = awtrix::test::freePort(SOCK_STREAM);
  require(os::startSockets(), "sockets");
  os::UniqueSocket held(::socket(AF_INET, SOCK_STREAM, 0));
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  address.sin_port = htons(static_cast<uint16_t>(port));
  require(held.valid() && ::bind(held.get(), reinterpret_cast<sockaddr*>(&address), sizeof address) == 0 &&
              ::listen(held.get(), 4) == 0,
          "another program listens on the port");

  Rig rig(port);
  TrayController controller(rig.host, hostArgs("busy", port), rig.view, rig.shell, rig.login,
                            [](int p, std::string& body) { return fetchStatus(p, body); }, {"1.3.0", port, ""});
  controller.begin(nowMs());
  const Watched first(rig.host.id());
  check(first.valid() && rig.view.state == HostState::Starting, "the host starts and the icon shows Starting");
  check(rig.signals.await(15000), "its end is signalled without anyone asking");
  controller.hostSignalled(nowMs());
  check(rig.view.state == HostState::Stopped &&
            rig.view.tip == "AWTRIX Script Host\nStopped: port " + std::to_string(port) + " is used by another program",
        "the icon shows the taken port");
  check(first.gone() && first.exitCode() == static_cast<DWORD>(hostd::kExitPortBusy) && rig.host.id() == -1,
        "the host is gone with the port-busy code");
  const int shows = rig.view.shows;
  controller.hostSignalled(nowMs());
  check(rig.view.state == HostState::Stopped && rig.view.shows == shows + 1, "a second signal changes nothing");

  const std::optional<MenuItem> again = item(controller.menu(nowMs()), MenuAction::Start);
  check(again && controller.choose(*again, nowMs()) && rig.view.state == HostState::Starting,
        "Start again starts a new host");
  check(rig.signals.await(15000), "whose end is signalled too");
  controller.hostSignalled(nowMs());
  check(rig.view.state == HostState::Stopped && rig.signals.count() == 2, "and stops it again for the same reason");
  controller.quit();
}

void quitEndsTheHostCleanly() {
  const int port = awtrix::test::freePort(SOCK_STREAM);
  Rig rig(port);
  TrayController controller(rig.host, hostArgs("quit", port), rig.view, rig.shell, rig.login,
                            [](int p, std::string& body) { return fetchStatus(p, body); }, {"1.3.0", port, ""});
  const int64_t start = nowMs();
  controller.begin(start);
  const Watched host(rig.host.id());
  const bool up = host.valid() && waitFor([&] { return answers(port); }, 15000);
  check(up, "the host answers");
  if (!up) {
    controller.quit();
    return;
  }
  int code = -1;
  check(!rig.host.ended(code), "a running host has not ended");
  controller.wake(start + HostSupervisor::kSettleMs);
  check(rig.view.state == HostState::Running, "after two seconds it is Running");
  const std::vector<MenuItem> items = controller.menu(nowMs());
  const std::optional<MenuItem> copy = item(items, MenuAction::CopyCode);
  check(copy && copy->text.rfind("Pairing code ", 0) == 0 && copy->value.size() == 6,
        "the menu holds the code read from the running host");
  check(std::any_of(items.begin(), items.end(), [](const MenuItem& i) { return i.text == "No clock paired yet"; }),
        "and says no clock is paired");

  const std::optional<MenuItem> quit = item(items, MenuAction::Quit);
  const auto before = std::chrono::steady_clock::now();
  check(quit && !controller.choose(*quit, nowMs()), "Quit ends the program");
  const auto took = std::chrono::steady_clock::now() - before;
  check(rig.journal.lines == std::vector<std::string>{"hide", "stop"}, "the icon goes first, then the host");
  check(host.gone() && host.exitCode() == 0u, "the host ended by itself with code 0 when its input closed");
  check(took < std::chrono::seconds(5), "well within the eight seconds before it would be killed");
  check(!answers(port), "its port is closed");
  check(rig.signals.count() == 0, "and quitting is not reported as an end");
}

void theAdapterAlone() {
  Signals signals;
  {
    TiedHost missing(signals.callback());
    check(!missing.start({(g_root / "no-such-program.exe").u8string()}), "a missing program does not start");
    check(!missing.start({}), "nor does an empty command line");
  }
  check(!signals.await(200) && signals.count() == 0, "neither signals an end");

  TiedHost broken(signals.callback());
  check(broken.start({g_host, "--port"}), "a host with a broken command line starts");
  check(signals.await(15000), "and its end is signalled");
  int code = -1;
  check(broken.ended(code) && code == hostd::kExitUsage, "with the usage code");
  check(!broken.ended(code), "an end is reported once");

  const int port = awtrix::test::freePort(SOCK_STREAM);
  long id = 0;
  {
    TiedHost running(signals.callback());
    check(running.start(hostArgs("scoped", port)) && waitFor([&] { return answers(port); }, 15000),
          "a host started in a scope answers");
    id = running.id();
  }
  const Watched gone(id);
  check(!gone.valid() || gone.gone(), "leaving the scope ends it");
  check(!answers(port), "and closes its port");
  check(signals.count() == 1, "without a signal");
}

}

int main(int argc, char** argv) {
  require(argc == 4, "usage: hostapp-win32-process-test AWTRIX_HOST AWTRIX_SCRIPT_RUNNER WEBUI");
  g_host = argv[1];
  g_runner = argv[2];
  g_webui = argv[3];
  g_root = fs::temp_directory_path() / ("awtrix-hostapp-win32-" + std::to_string(::GetCurrentProcessId()));
  std::error_code ignored;
  fs::remove_all(g_root, ignored);
  fs::create_directories(g_root);
  aTakenPortStopsIt();
  quitEndsTheHostCleanly();
  theAdapterAlone();
  fs::remove_all(g_root, ignored);
  return awtrix::test::finish("the Windows tray program with the real awtrix-host");
}
