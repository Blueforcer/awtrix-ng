#include "../support.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "platform/host/app/StatusMenu.h"
#include "platform/host/app/win32/HostLayout.h"
#include "platform/host/app/win32/MenuText.h"
#include "platform/host/app/win32/RunRegistry.h"
#include "platform/host/app/win32/StartAtLogin.h"
#include "platform/host/app/win32/TrayController.h"
#include "platform/host/app/win32/TrayImage.h"

using namespace awtrix::hostapp;
using namespace awtrix::hostapp::win32;
using awtrix::test::check;

namespace {

const std::wstring kCommand = L"\"C:\\Program Files\\AWTRIX Script Host\\AWTRIX Script Host.exe\"";

class FakeRegistry : public LoginRegistry {
 public:
  std::optional<bool> chosen;
  std::optional<std::wstring> run;
  bool blocked = false;
  bool failWrites = false;
  int writes = 0;

  std::optional<bool> choice() override { return chosen; }
  bool remember(bool on) override {
    if (failWrites) return false;
    ++writes;
    chosen = on;
    return true;
  }
  std::optional<std::wstring> command() override { return run; }
  bool setCommand(const std::wstring& command) override {
    if (failWrites) return false;
    ++writes;
    run = command;
    return true;
  }
  bool removeCommand() override {
    if (failWrites) return false;
    ++writes;
    run.reset();
    return true;
  }
  bool blockedByWindows() override { return blocked; }
  bool unblock() override {
    if (failWrites) return false;
    ++writes;
    blocked = false;
    return true;
  }
};

void firstRunTurnsStartAtLoginOn() {
  FakeRegistry registry;
  StartAtLogin login(registry, kCommand);
  check(!login.enabled(), "nothing starts at login before the first run");
  login.settle();
  check(registry.run == kCommand && registry.chosen == true, "the first run adds the Run value and remembers it");
  check(login.enabled(), "and shows Start at login ticked");
  const int writes = registry.writes;
  login.settle();
  check(registry.writes == writes, "a later start with the same path writes nothing");
}

void theUsersChoiceStays() {
  FakeRegistry registry;
  StartAtLogin login(registry, kCommand);
  login.settle();
  check(login.set(false) && !registry.run && registry.chosen == false, "turning it off removes the Run value");
  login.settle();
  check(!registry.run && !login.enabled(), "and a later start keeps it off");
  check(login.set(true) && registry.run == kCommand && registry.chosen == true && login.enabled(),
        "turning it on again adds the value back");
}

void aMovedProgramIsFollowed() {
  FakeRegistry registry;
  registry.chosen = true;
  registry.run = L"\"D:\\old\\AWTRIX Script Host.exe\"";
  StartAtLogin login(registry, kCommand);
  check(!login.enabled(), "a Run value for another path is not this program's");
  login.settle();
  check(registry.run == kCommand && login.enabled(), "settle points it at this program");

  FakeRegistry removed;
  removed.chosen = true;
  StartAtLogin again(removed, kCommand);
  again.settle();
  check(removed.run == kCommand, "a Run value removed by another tool comes back while the choice is on");

  FakeRegistry off;
  off.chosen = false;
  off.run = kCommand;
  StartAtLogin stale(off, kCommand);
  stale.settle();
  check(!off.run, "a Run value left behind while the choice is off is removed");
}

void windowsCanBlockIt() {
  FakeRegistry registry;
  StartAtLogin login(registry, kCommand);
  login.settle();
  registry.blocked = true;
  check(!login.enabled(), "a program turned off in Task Manager is not shown as starting at login");
  login.settle();
  check(registry.blocked && registry.run == kCommand, "settle leaves the Task Manager choice alone");
  check(login.set(true) && !registry.blocked && login.enabled(), "choosing Start at login lifts the block");
}

void failedWritesShowTheTruth() {
  FakeRegistry registry;
  registry.failWrites = true;
  StartAtLogin login(registry, kCommand);
  login.settle();
  check(!registry.chosen && !registry.run, "a first run that cannot write remembers nothing");
  check(!login.enabled(), "and shows Start at login unticked");
  registry.failWrites = false;
  login.settle();
  check(registry.chosen == true && login.enabled(), "the next start tries the first run again");
  registry.failWrites = true;
  check(!login.set(false) && login.enabled(), "a refused change reports failure and keeps the tick");
}

class ScratchKey {
 public:
  ScratchKey() : root_(L"Software\\AWTRIX Script Host Tests\\" + std::to_wstring(::GetCurrentProcessId())) {}
  ~ScratchKey() {
    ::RegDeleteTreeW(HKEY_CURRENT_USER, root_.c_str());
    ::RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\AWTRIX Script Host Tests");
  }
  RunPlaces places() const { return {root_ + L"\\Run", root_ + L"\\Approved", root_ + L"\\Settings", L"Probe"}; }
  bool setApproved(BYTE first) const {
    const BYTE data[12] = {first};
    return ::RegSetKeyValueW(HKEY_CURRENT_USER, (root_ + L"\\Approved").c_str(), L"Probe", REG_BINARY, data,
                             sizeof data) == ERROR_SUCCESS;
  }
  bool setRunNumber() const {
    const DWORD number = 7;
    return ::RegSetKeyValueW(HKEY_CURRENT_USER, (root_ + L"\\Run").c_str(), L"Probe", REG_DWORD, &number,
                             sizeof number) == ERROR_SUCCESS;
  }
  bool exists() const {
    HKEY key = nullptr;
    if (::RegOpenKeyExW(HKEY_CURRENT_USER, root_.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS) return false;
    ::RegCloseKey(key);
    return true;
  }

 private:
  std::wstring root_;
};

void realRegistry() {
  {
    ScratchKey scratch;
    RunRegistry registry(scratch.places());
    check(!registry.choice() && !registry.command() && !registry.blockedByWindows(), "an empty key holds nothing");
    check(registry.removeCommand() && registry.unblock(), "removing what is not there succeeds");
    check(registry.remember(false) && registry.choice() == false, "the choice is remembered as off");
    check(registry.remember(true) && registry.choice() == true, "and as on");
    const std::wstring command = L"\"C:\\Pfad mit \u00fc\\AWTRIX Script Host.exe\"";
    check(registry.setCommand(command) && registry.command() == command, "the Run value keeps a non-ASCII path");
    check(registry.removeCommand() && !registry.command(), "and is removed");
    check(scratch.setApproved(0x03) && registry.blockedByWindows(), "a Task Manager block (03) is seen");
    check(registry.unblock() && !registry.blockedByWindows(), "and lifted");
    check(scratch.setApproved(0x02) && !registry.blockedByWindows(), "an approval (02) is not a block");
    check(scratch.setRunNumber() && !registry.command(), "a Run value that is not text is not a command");
    StartAtLogin login(registry, command);
    login.settle();
    check(registry.command() == command && login.enabled(), "the first run over the real registry works");
    check(scratch.exists(), "the test wrote below its own key");
  }
  check(!ScratchKey().exists(), "and removed it again");
  const RunPlaces product = loginPlaces();
  check(product.run == L"Software\\Microsoft\\Windows\\CurrentVersion\\Run" && product.name == L"AWTRIX Script Host" &&
            product.settings == L"Software\\AWTRIX Script Host" &&
            product.approved == L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run",
        "the product uses the Run key, its StartupApproved entry and its own key");
}

void menuTexts() {
  check(menuLabel("Running") == L"Running", "plain text stays");
  check(menuLabel("Tom & Jerry \xC2\xB7 192.168.1.5 \xC2\xB7 running") == L"Tom && Jerry \u00b7 192.168.1.5 \u00b7 running",
        "an ampersand is doubled so it is not a mnemonic, the middle dot survives");
  check(menuLabel("&&") == L"&&&&", "every ampersand is doubled");
  check(menuLabel("Starting\xE2\x80\xA6") == L"Starting\u2026", "the ellipsis survives");
  check(menuLabel("K\xFF" "che") == L"K\uFFFD" L"che", "broken UTF-8 becomes a replacement character");
  check(menuLabel("Uhr \xF0\x9F\x95\x92") == L"Uhr \U0001F552", "characters outside the BMP become surrogate pairs");
  check(menuLabel("").empty(), "an empty text stays empty");

  const std::string line = "AWTRIX Script Host\nStopped: port 8300 is used by another program";
  check(tipText(line) == L"AWTRIX Script Host\nStopped: port 8300 is used by another program",
        "the tooltip keeps its line break and its text");
  check(tipText(std::string(200, 'x')).size() == kTipChars, "a long tooltip is cut to what the shell holds");
  std::string pairs;
  for (int i = 0; i < 70; ++i) pairs += "\xF0\x9F\x95\x92";
  const std::wstring cut = tipText(pairs);
  check(cut.size() == kTipChars - 1 && cut.back() >= 0xDC00 && cut.back() <= 0xDFFF,
        "a cut tooltip does not end in half a surrogate pair");
}

void iconImages() {
  for (const int size : {16, 20, 24, 32, 40, 48}) {
    const std::vector<uint32_t> up = iconPixels(HostState::Running, size);
    const std::vector<uint32_t> down = iconPixels(HostState::Stopped, size);
    check(up.size() == static_cast<std::size_t>(size) * size, "one pixel per point");
    const auto lit = std::count_if(up.begin(), up.end(), [](uint32_t p) { return p != 0; });
    check(lit > 0 && lit < static_cast<long>(up.size()), "some pixels lit, some clear");
    check(std::all_of(up.begin(), up.end(), [](uint32_t p) { return p == 0 || p == 0xFF7DC242; }),
          "a running host is green and opaque");
    check(std::all_of(down.begin(), down.end(), [](uint32_t p) { return p == 0 || p == 0xFF8C8C8C; }),
          "a stopped host is grey");
    const HICON icon = makeIcon(HostState::Starting, size);
    ICONINFO info{};
    BITMAP color{};
    const bool made = icon && ::GetIconInfo(icon, &info) &&
                      ::GetObjectW(info.hbmColor, sizeof color, &color) == sizeof color;
    check(made && color.bmWidth == size && color.bmHeight == size && color.bmBitsPixel == 32,
          "the icon is a 32-bit image of that size");
    if (info.hbmColor) ::DeleteObject(info.hbmColor);
    if (info.hbmMask) ::DeleteObject(info.hbmMask);
    if (icon) ::DestroyIcon(icon);
  }
  check(iconPixels(HostState::Running, 4).empty() && !makeIcon(HostState::Running, 4), "too small gives no icon");
}

void layout() {
  const HostLayout paths{L"C:\\Program Files\\AWTRIX Script Host", L"C:\\Users\\J\u00fcrgen\\AppData\\Local\\AWTRIX Script Host"};
  const std::string data = "C:\\Users\\J\xC3\xBCrgen\\AppData\\Local\\AWTRIX Script Host";
  const std::string program = "C:\\Program Files\\AWTRIX Script Host";
  const std::vector<std::string> expected = {program + "\\awtrix-host.exe",
                                             "--state",
                                             data,
                                             "--log",
                                             data + "\\awtrix-host.log",
                                             "--exit-with-stdin",
                                             "--binary",
                                             program + "\\awtrix-script-runner.exe",
                                             "--webui",
                                             program + "\\webui\\index.html",
                                             "--ca-file",
                                             program + "\\ca-certificates.crt"};
  check(paths.hostArgs() == expected, "the host runs from the program folder with its data, log and web UI");
  check(paths.log().u8string() == data + "\\awtrix-host.log", "the log lies in the data folder");
}

class FakeProcess : public WatchedProcess {
 public:
  std::vector<std::string>* journal = nullptr;
  bool startWorks = true;
  int starts = 0;
  int stops = 0;
  std::vector<std::string> args;
  std::optional<int> exit;

  bool start(const std::vector<std::string>& a) override {
    ++starts;
    args = a;
    return startWorks;
  }
  void stop() override {
    ++stops;
    if (journal) journal->push_back("stop");
  }
  bool ended(int& code) override {
    if (!exit) return false;
    code = *exit;
    exit.reset();
    return true;
  }
};

class FakeView : public TrayView {
 public:
  std::vector<std::string>* journal = nullptr;
  std::optional<HostState> state;
  std::string tip;
  std::optional<int64_t> wake;
  int shows = 0;
  bool hidden = false;

  void show(HostState s, const std::string& t) override {
    ++shows;
    state = s;
    tip = t;
  }
  void wakeIn(std::optional<int64_t> delay) override { wake = delay; }
  void hide() override {
    hidden = true;
    if (journal) journal->push_back("hide");
  }
};

class FakeShell : public Shell {
 public:
  std::vector<std::string> opened;
  std::vector<std::string> copied;
  bool logExists = true;

  bool open(const std::string& target) override {
    opened.push_back(target);
    return true;
  }
  bool copy(const std::string& text) override {
    copied.push_back(text);
    return true;
  }
  bool exists(const std::string&) override { return logExists; }
};

const std::string kStatus =
    R"({"name":"pc","version":"1.3.0","clocks":[{"uid":"c0ffee","name":"Tom & Jerry","clock":"http://192.168.1.39",)"
    R"("width":32,"height":8,"port":8301,"running":true,"waiting":false}],"code":"042517","codeValidSeconds":512})";

struct Rig {
  FakeProcess process;
  FakeView view;
  FakeShell shell;
  FakeRegistry registry;
  StartAtLogin login{registry, kCommand};
  int fetches = 0;
  bool fetchWorks = true;
  TrayController controller{process,
                            {"awtrix-host.exe", "--exit-with-stdin"},
                            view,
                            shell,
                            login,
                            [this](int port, std::string& body) {
                              ++fetches;
                              if (port != 8300 || !fetchWorks) return false;
                              body = kStatus;
                              return true;
                            },
                            {"1.3.0", 8300, "C:\\data\\awtrix-host.log"}};
};

const MenuItem* find(const std::vector<MenuItem>& items, MenuAction action) {
  const auto it = std::find_if(items.begin(), items.end(), [action](const MenuItem& i) { return i.action == action; });
  return it == items.end() ? nullptr : &*it;
}

bool has(const std::vector<MenuItem>& items, const std::string& text) {
  return std::any_of(items.begin(), items.end(), [&](const MenuItem& i) { return i.text == text; });
}

void controllerRuns() {
  Rig rig;
  rig.controller.begin(1000);
  check(rig.process.starts == 1 && rig.process.args == std::vector<std::string>{"awtrix-host.exe", "--exit-with-stdin"},
        "begin starts the host with its arguments");
  check(rig.view.state == HostState::Starting && rig.view.tip == "AWTRIX Script Host\nStarting\xE2\x80\xA6",
        "the icon shows Starting");
  check(rig.view.wake == HostSupervisor::kSettleMs, "and one timer for the end of the start");
  rig.controller.wake(1000 + HostSupervisor::kSettleMs);
  check(rig.view.state == HostState::Running && rig.view.tip == "AWTRIX Script Host\nRunning" && !rig.view.wake,
        "the timer makes it Running, with no timer left");

  const int shows = rig.view.shows;
  rig.controller.hostSignalled(5000);
  check(rig.view.state == HostState::Running && rig.view.shows == shows + 1,
        "a signal for a host that has not ended changes nothing");

  const std::vector<MenuItem> items = rig.controller.menu(6000);
  check(rig.fetches == 1, "opening the menu of a running host fetches its status once");
  const MenuItem* code = find(items, MenuAction::CopyCode);
  check(code && code->text == "Pairing code 042517" && code->value == "042517", "the menu offers the code");
  check(has(items, "Tom & Jerry \xC2\xB7 192.168.1.39 \xC2\xB7 running"), "and the clock");
  const MenuItem* log = find(items, MenuAction::OpenLog);
  check(log && log->enabled && log->value == "C:\\data\\awtrix-host.log", "and the log that exists");
  const MenuItem* login = find(items, MenuAction::StartAtLogin);
  check(login && !login->checked, "Start at login shows the registry, here not set");

  check(rig.controller.choose(*code, 6000) && rig.shell.copied == std::vector<std::string>{"042517"},
        "choosing the code copies it");
  const MenuItem* page = find(items, MenuAction::OpenStatusPage);
  check(page && rig.controller.choose(*page, 6000) && rig.shell.opened.back() == "http://127.0.0.1:8300/",
        "the status page opens");
  const MenuItem* clock = find(items, MenuAction::OpenClock);
  check(clock && rig.controller.choose(*clock, 6000) && rig.shell.opened.back() == "http://192.168.1.39",
        "a clock opens its web UI");
  check(rig.controller.choose(*log, 6000) && rig.shell.opened.back() == "C:\\data\\awtrix-host.log", "the log opens");
  check(rig.controller.choose(*login, 6000) && rig.registry.run == kCommand && rig.registry.chosen == true,
        "Start at login turns it on");
  const std::vector<MenuItem> after = rig.controller.menu(7000);
  const MenuItem* ticked = find(after, MenuAction::StartAtLogin);
  check(ticked && ticked->checked, "and the next menu shows the tick");
  check(rig.controller.choose(*ticked, 7000) && !rig.registry.run && rig.registry.chosen == false,
        "choosing it again turns it off");
  MenuItem greyed = *page;
  greyed.enabled = false;
  const std::size_t opened = rig.shell.opened.size();
  check(rig.controller.choose(greyed, 7000) && rig.shell.opened.size() == opened, "a greyed item does nothing");
}

void controllerMenuWithoutStatus() {
  Rig rig;
  rig.shell.logExists = false;
  rig.fetchWorks = false;
  rig.controller.begin(0);
  const std::vector<MenuItem> items = rig.controller.menu(10);
  check(rig.fetches == 1 && !find(items, MenuAction::CopyCode), "a status that cannot be read leaves out the code");
  const MenuItem* log = find(items, MenuAction::OpenLog);
  check(log && !log->enabled, "a log that does not exist yet is greyed");
}

void controllerPortBusy() {
  Rig rig;
  rig.controller.begin(0);
  rig.process.exit = awtrix::hostd::kExitPortBusy;
  rig.controller.hostSignalled(300);
  check(rig.view.state == HostState::Stopped &&
            rig.view.tip == "AWTRIX Script Host\nStopped: port 8300 is used by another program" && !rig.view.wake,
        "a taken port stops it with that reason and no timer");
  const std::vector<MenuItem> items = rig.controller.menu(400);
  check(rig.fetches == 0, "the menu of a stopped host does not ask for its status");
  const MenuItem* again = find(items, MenuAction::Start);
  check(again && again->text == "Start again" && again->enabled, "the menu offers Start again");
  const MenuItem* page = find(items, MenuAction::OpenStatusPage);
  check(page && !page->enabled, "and greys the status page");
  check(rig.controller.choose(*again, 500) && rig.process.starts == 2 && rig.view.state == HostState::Starting,
        "Start again starts the host");
}

void controllerRestarts() {
  Rig rig;
  rig.controller.begin(0);
  rig.process.exit = 1;
  rig.controller.hostSignalled(1000);
  check(rig.view.state == HostState::Restarting && rig.view.tip == "AWTRIX Script Host\nRestarting in 5 s\xE2\x80\xA6" &&
            rig.view.wake == 5000,
        "a crash shows the restart countdown and sets the timer for it");
  check(rig.controller.tip(3500) == "AWTRIX Script Host\nRestarting in 3 s\xE2\x80\xA6", "the tooltip counts down");
  rig.controller.wake(4000);
  check(rig.process.starts == 1 && rig.view.wake == 2000, "an early timer only sets the rest");
  rig.controller.wake(6000);
  check(rig.process.starts == 2 && rig.view.state == HostState::Starting, "the timer restarts the host");
}

void controllerCannotStart() {
  Rig rig;
  rig.process.startWorks = false;
  rig.controller.begin(0);
  check(rig.view.state == HostState::Stopped &&
            rig.view.tip == "AWTRIX Script Host\nStopped: the script host cannot start" && !rig.view.wake,
        "a host that does not start is reported");
  check(find(rig.controller.menu(1), MenuAction::Start), "with Start again");
}

void controllerQuits() {
  Rig rig;
  std::vector<std::string> journal;
  rig.view.journal = &journal;
  rig.process.journal = &journal;
  rig.controller.begin(0);
  const std::vector<MenuItem> items = rig.controller.menu(10);
  const MenuItem* quit = find(items, MenuAction::Quit);
  check(quit && !rig.controller.choose(*quit, 20), "Quit ends the program");
  check(journal == std::vector<std::string>{"hide", "stop"}, "the icon goes before the host is stopped");
  check(!rig.view.wake, "and no timer is left");
  const int shows = rig.view.shows;
  rig.process.exit = 0;
  rig.controller.hostSignalled(30);
  rig.controller.wake(40);
  rig.controller.quit();
  check(rig.view.shows == shows && journal.size() == 2 && rig.process.starts == 1,
        "after Quit nothing shows the icon, stops or starts the host again");
  const MenuItem* page = find(items, MenuAction::OpenStatusPage);
  check(page && !rig.controller.choose(*page, 50) && rig.shell.opened.empty(), "nor does a late menu choice");
}

}

int main() {
  firstRunTurnsStartAtLoginOn();
  theUsersChoiceStays();
  aMovedProgramIsFollowed();
  windowsCanBlockIt();
  failedWritesShowTheTruth();
  realRegistry();
  menuTexts();
  iconImages();
  layout();
  controllerRuns();
  controllerMenuWithoutStatus();
  controllerPortBusy();
  controllerRestarts();
  controllerCannotStart();
  controllerQuits();
  return awtrix::test::finish("the Windows tray program's logic");
}
