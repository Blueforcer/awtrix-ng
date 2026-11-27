#include "../support.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "platform/host/app/StatusMenu.h"
#include "platform/host/app/TrayIcon.h"

using namespace awtrix::hostapp;
using awtrix::test::check;

namespace {

const std::string kStatus =
    R"({"name":"desk","version":"1.3.0","clocks":[)"
    R"({"uid":"c0ffee","name":"Kitchen","clock":"http://192.168.1.39","width":32,"height":8,"port":8301,)"
    R"("running":true,"waiting":false},)"
    R"({"uid":"beef","name":"","clock":"http://192.168.1.40/","width":32,"height":8,"port":8302,)"
    R"("running":false,"waiting":true},)"
    R"({"uid":"abc","name":"Hall","clock":"http://192.168.1.41","width":32,"height":8,"port":8303,)"
    R"("running":false,"waiting":false})"
    R"(],"code":"042517","codeValidSeconds":512})";

MenuInput input(std::string_view status, HostState state = HostState::Running) {
  MenuInput in;
  in.version = "1.3.0";
  in.port = 8300;
  in.host.state = state;
  in.status = status;
  in.logPath = "C:/Users/me/AppData/Local/AWTRIX Script Host/awtrix-host.log";
  return in;
}

std::vector<std::string> texts(const std::vector<MenuItem>& items) {
  std::vector<std::string> out;
  for (const MenuItem& item : items) out.push_back(item.separator ? "-" : item.text);
  return out;
}

const MenuItem* find(const std::vector<MenuItem>& items, MenuAction action) {
  const auto it = std::find_if(items.begin(), items.end(), [action](const MenuItem& i) { return i.action == action; });
  return it == items.end() ? nullptr : &*it;
}

bool opens(const MenuItem& item, const std::string& url) {
  return item.enabled && item.action == MenuAction::OpenClock && item.value == url;
}

bool inert(const MenuItem& item) { return !item.enabled && item.action == MenuAction::None && item.value.empty(); }

bool tidy(const std::vector<MenuItem>& items) {
  if (items.empty() || items.front().separator || items.back().separator) return false;
  for (std::size_t i = 1; i < items.size(); ++i)
    if (items[i].separator && items[i - 1].separator) return false;
  return true;
}

const std::vector<std::string> kBare = {"AWTRIX Script Host 1.3.0", "Running", "-", "Open status page",
                                        "Open log", "Start at login", "-", "Quit"};

void fullStatus() {
  MenuInput in = input(kStatus);
  in.startAtLogin = true;
  const std::vector<MenuItem> items = buildStatusMenu(in);
  const std::vector<std::string> expected = {"AWTRIX Script Host 1.3.0",
                                             "Running",
                                             "-",
                                             "Pairing code 042517",
                                             "Kitchen \u00b7 192.168.1.39 \u00b7 running",
                                             "beef \u00b7 192.168.1.40 \u00b7 waiting",
                                             "Hall \u00b7 192.168.1.41 \u00b7 restarting",
                                             "-",
                                             "Open status page",
                                             "Open log",
                                             "Start at login",
                                             "-",
                                             "Quit"};
  check(texts(items) == expected, "the menu lists title, status, code, clocks and commands in order");
  check(!items[0].enabled && !items[1].enabled, "title and status are information only");
  check(!find(items, MenuAction::Start), "a running host offers no start");
  check(opens(items[4], "http://192.168.1.39") && opens(items[5], "http://192.168.1.40/") &&
            opens(items[6], "http://192.168.1.41"),
        "a clock line opens the clock's web UI at its address");
  const MenuItem* code = find(items, MenuAction::CopyCode);
  check(code && code->enabled && code->value == "042517", "the code line copies the code");
  const MenuItem* page = find(items, MenuAction::OpenStatusPage);
  check(page && page->enabled && page->value == "http://127.0.0.1:8300/", "the status page opens on this computer");
  const MenuItem* log = find(items, MenuAction::OpenLog);
  check(log && log->enabled && log->value == in.logPath, "the log opens at the path the program gave");
  const MenuItem* login = find(items, MenuAction::StartAtLogin);
  check(login && login->enabled && login->checkable && login->checked, "start at login shows its tick");
  in.startAtLogin = false;
  check(!find(buildStatusMenu(in), MenuAction::StartAtLogin)->checked, "and none when it is off");
  const MenuItem* quit = find(items, MenuAction::Quit);
  check(quit && quit->enabled && !quit->checkable, "quit is always there");
  check(tidy(items), "no separator at an end or twice in a row");
}

void partialStatus() {
  std::vector<std::string> expected = kBare;
  expected.insert(expected.begin() + 3, {"Pairing code 123456", "No clock paired yet", "-"});
  check(texts(buildStatusMenu(input(R"({"clocks":[],"code":"123456"})"))) == expected,
        "no clocks yet says so, below the code");

  expected = kBare;
  expected.insert(expected.begin() + 3, {"No clock paired yet", "-"});
  check(texts(buildStatusMenu(input(R"({"clocks":[]})"))) == expected, "a status without a code shows no code line");

  expected = kBare;
  expected.insert(expected.begin() + 3, {"Pairing code 123456", "-"});
  check(texts(buildStatusMenu(input(R"({"code":"123456"})"))) == expected,
        "a status without clocks claims none, neither clocks nor no clocks");

  expected = kBare;
  expected.insert(expected.begin() + 3, {"ab \u00b7 restarting", "Desk Two \u00b7 10.0.0.2 \u00b7 running", "-"});
  const std::vector<MenuItem> odd =
      buildStatusMenu(input(R"({"code":123456,"clocks":[1,"x",{"uid":"ab","running":"yes"},)"
                            R"({"name":"Desk\nTwo","clock":"10.0.0.2","running":true}]})"));
  check(texts(odd) == expected,
        "odd members fall back: a numeric code, non-object clocks, no address, a name over two lines");
  check(inert(odd[3]) && inert(odd[4]), "a clock without an http:// address does not open");

  const std::vector<MenuItem> urls = buildStatusMenu(input(
      R"({"clocks":[{"name":"a","clock":"https://10.0.0.3"},{"name":"b","clock":"http://"},)"
      R"({"name":"c","clock":"http://10.0.0.4 /x"},{"name":"d","clock":"http://10.0.0.5\u0001"},)"
      R"({"name":"e","clock":"javascript:alert"},{"name":"f","clock":7},)"
      R"({"name":"g","clock":"http://)" + std::string(250, 'x') + R"("},)"
      R"({"name":"h","clock":"http://10.0.0.6:8080"}]})"));
  check(urls.size() == kBare.size() + 9, "every odd address still gets its clock line");
  bool none = true;
  for (std::size_t i = 3; i < 10; ++i) none = none && inert(urls[i]);
  check(none, "https, no host, spaces, control characters, other schemes, numbers and overlong addresses do not open");
  check(opens(urls[10], "http://10.0.0.6:8080"), "an address with a port opens");

  expected = kBare;
  expected.insert(expected.begin() + 3, {"Clock \u00b7 running", "-"});
  check(texts(buildStatusMenu(input(R"({"code":"12a456","clocks":[{"name":"  ","running":true}]})"))) == expected,
        "a code that is not digits is left out and a clock without name or uid still gets a line");
}

void unusableStatus() {
  for (const char* status : {"", "{\"code\":\"123456\",\"clocks\":[", "<html>busy</html>", "[1,2]", "null",
                             "{\"code\":\"123456\"} trailing", "\"text\""}) {
    const std::vector<MenuItem> items = buildStatusMenu(input(status));
    check(texts(items) == kBare && !find(items, MenuAction::CopyCode),
          std::string("an unusable status gives the plain menu: ") + status);
    check(tidy(items), "and stays tidy");
  }
}

void hostDown() {
  MenuInput in = input(kStatus, HostState::Stopped);
  in.host.reason = StopReason::PortBusy;
  in.port = 8400;
  std::vector<MenuItem> items = buildStatusMenu(in);
  check(items[1].text == "Stopped: port 8400 is used by another program", "a taken port is named in the status");
  check(!find(items, MenuAction::CopyCode) && !find(items, MenuAction::OpenClock) && items.size() == kBare.size() + 1,
        "a stopped host shows no code or clocks, even from an old status");
  check(items[2].text == "Start again" && items[2].enabled && items[2].action == MenuAction::Start && tidy(items),
        "a taken port offers to start again, right below the status");
  in.host.reason = StopReason::CannotStart;
  items = buildStatusMenu(in);
  check(items[2].action == MenuAction::Start && items[2].enabled, "so does a host that did not start");
  for (const StopReason reason : {StopReason::Quit, StopReason::None}) {
    in.host.reason = reason;
    check(!find(buildStatusMenu(in), MenuAction::Start), "quit or never started offers no start");
  }
  in.host.reason = StopReason::PortBusy;
  items = buildStatusMenu(in);
  const MenuItem* page = find(items, MenuAction::OpenStatusPage);
  check(page && !page->enabled && page->value == "http://127.0.0.1:8400/", "and no status page to open");
  check(find(items, MenuAction::OpenLog)->enabled && find(items, MenuAction::Quit)->enabled,
        "the log and quit stay");

  in.host.state = HostState::Restarting;
  in.host.reason = StopReason::None;
  in.host.restartInMs = 9500;
  items = buildStatusMenu(in);
  check(items[1].text == "Restarting in 10 s\u2026" && !find(items, MenuAction::OpenStatusPage)->enabled,
        "a restart counts its seconds, rounded up");
  check(!find(items, MenuAction::Start), "and offers no start, it starts by itself");
  in.logPath.clear();
  check(!find(buildStatusMenu(in), MenuAction::OpenLog)->enabled, "without a log path the log does not open");
}

void statusLines() {
  HostView view;
  view.state = HostState::Starting;
  check(statusLine(view, 8300) == "Starting\u2026", "starting");
  view.state = HostState::Running;
  check(statusLine(view, 8300) == "Running", "running");
  view.state = HostState::Restarting;
  view.restartInMs = 0;
  check(statusLine(view, 8300) == "Restarting in 1 s\u2026", "a restart that is due shows one second");
  view.restartInMs = 60000;
  check(statusLine(view, 8300) == "Restarting in 60 s\u2026", "a minute");
  view.state = HostState::Stopped;
  view.reason = StopReason::CannotStart;
  check(statusLine(view, 8300) == "Stopped: the script host cannot start", "a host that does not start");
  view.reason = StopReason::Quit;
  check(statusLine(view, 8300) == "Stopped", "quit");
  view.reason = StopReason::None;
  check(statusLine(view, 8300) == "Stopped", "never started");
  MenuInput unnamed = input("", HostState::Starting);
  unnamed.version.clear();
  check(buildStatusMenu(unnamed)[0].text == "AWTRIX Script Host", "a title without a version");
}

void icon() {
  int lit = 0;
  for (int row = 0; row < kIconLeds; ++row)
    for (int column = 0; column < kIconLeds; ++column) lit += iconLit(column, row);
  check(lit > 0 && lit < kIconLeds * kIconLeds, "the pattern lights some LEDs, not all");
  check(!iconLit(-1, 0) && !iconLit(0, kIconLeds), "outside the pattern nothing is lit");
  const auto opaque = [](const std::vector<uint8_t>& alpha) {
    return std::count(alpha.begin(), alpha.end(), uint8_t{255});
  };
  const auto clear = [](const std::vector<uint8_t>& alpha) {
    return std::count(alpha.begin(), alpha.end(), uint8_t{0});
  };
  const std::vector<uint8_t> small = iconAlpha(16);
  check(small.size() == 256 && opaque(small) == lit * 4 && opaque(small) + clear(small) == 256,
        "at 16 px every lit LED is a solid 2 x 2 block");
  const std::vector<uint8_t> large = iconAlpha(32);
  check(large.size() == 1024 && opaque(large) == lit * 9, "from 4 px per LED a gap shows between LEDs");
  const std::vector<uint8_t> odd = iconAlpha(20);
  const auto blank = [](uint8_t a) { return a == 0; };
  check(odd.size() == 400 && opaque(odd) == lit * 4 && std::all_of(odd.begin(), odd.begin() + 40, blank) &&
            std::all_of(odd.end() - 40, odd.end(), blank),
        "a size between steps keeps whole LEDs and centres them");
  check(iconAlpha(7).empty() && iconAlpha(4096).empty(), "sizes it cannot draw give nothing");

  const uint32_t up = iconColor(HostState::Running);
  const uint32_t down = iconColor(HostState::Stopped);
  check(iconColor(HostState::Starting) == up && iconColor(HostState::Restarting) == down && up != down,
        "running and starting share a colour, restarting and stopped another");
  check((down >> 16 & 0xFF) == (down >> 8 & 0xFF) && (down >> 8 & 0xFF) == (down & 0xFF), "the stopped colour is grey");
}

}

int main() {
  fullStatus();
  partialStatus();
  unusableStatus();
  hostDown();
  statusLines();
  icon();
  return awtrix::test::finish("host tray menu and icon");
}
