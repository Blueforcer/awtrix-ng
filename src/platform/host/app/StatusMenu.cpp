#include "platform/host/app/StatusMenu.h"

#include <algorithm>
#include <utility>

#include "core/api/JsonReader.h"

namespace awtrix::hostapp {
namespace {

constexpr std::size_t kMaxCodeDigits = 12;
constexpr std::size_t kMaxUrl = 256;
constexpr const char kDot[] = " \xC2\xB7 ";

struct Status {
  std::string code;
  bool clocksKnown = false;
  std::vector<MenuItem> clocks;
};

std::string oneLine(std::string text) {
  for (char& c : text)
    if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f) c = ' ';
  const auto first = text.find_first_not_of(' ');
  if (first == std::string::npos) return {};
  return text.substr(first, text.find_last_not_of(' ') - first + 1);
}

bool clickableUrl(const std::string& url) {
  return url.size() > 7 && url.size() <= kMaxUrl && url.rfind("http://", 0) == 0 &&
         std::all_of(url.begin(), url.end(), [](char c) { return c > 0x20 && c < 0x7f; });
}

bool validCode(const std::string& code) {
  return !code.empty() && code.size() <= kMaxCodeDigits &&
         std::all_of(code.begin(), code.end(), [](char c) { return c >= '0' && c <= '9'; });
}

MenuItem clockLine(const api::JsonReader& clock) {
  std::string name = oneLine(api::memberText(clock, "name"));
  if (name.empty()) name = oneLine(api::memberText(clock, "uid"));
  if (name.empty()) name = "Clock";
  const std::string url = api::memberText(clock, "clock");
  std::string address = oneLine(url);
  if (address.rfind("http://", 0) == 0) address.erase(0, 7);
  while (!address.empty() && address.back() == '/') address.pop_back();
  bool running = false, waiting = false;
  api::memberValue(clock, "running").asBool(running);
  api::memberValue(clock, "waiting").asBool(waiting);
  std::string line = name;
  if (!address.empty()) line += kDot + address;
  line += kDot;
  line += running ? "running" : waiting ? "waiting" : "restarting";
  MenuItem item;
  item.text = std::move(line);
  if (clickableUrl(url)) {
    item.enabled = true;
    item.action = MenuAction::OpenClock;
    item.value = url;
  }
  return item;
}

bool readStatus(std::string_view json, Status& out) {
  if (json.empty() || !api::isWellFormed(json)) return false;
  const api::JsonReader root(json);
  if (!root.isObject()) return false;
  const std::string code = api::memberText(root, "code");
  if (validCode(code)) out.code = code;
  api::JsonReader clocks = api::memberValue(root, "clocks");
  if (!clocks.isArray() || !clocks.enterArray()) return true;
  out.clocksKnown = true;
  while (clocks.nextElement()) {
    if (clocks.isObject()) out.clocks.push_back(clockLine(clocks));
    if (!clocks.skipValue()) break;
  }
  return true;
}

MenuItem info(std::string text) {
  MenuItem item;
  item.text = std::move(text);
  return item;
}

MenuItem command(std::string text, MenuAction action, std::string value = {}, bool enabled = true) {
  MenuItem item;
  item.text = std::move(text);
  item.enabled = enabled;
  item.action = action;
  item.value = std::move(value);
  return item;
}

MenuItem separator() {
  MenuItem item;
  item.separator = true;
  return item;
}

}

std::string statusLine(const HostView& host, int port) {
  switch (host.state) {
    case HostState::Starting:
      return "Starting\xE2\x80\xA6";
    case HostState::Running:
      return "Running";
    case HostState::Restarting:
      return "Restarting in " + std::to_string(std::max<int64_t>(1, (host.restartInMs + 999) / 1000)) +
             " s\xE2\x80\xA6";
    case HostState::Stopped:
      break;
  }
  switch (host.reason) {
    case StopReason::PortBusy:
      return "Stopped: port " + std::to_string(port) + " is used by another program";
    case StopReason::CannotStart:
      return "Stopped: the script host cannot start";
    default:
      return "Stopped";
  }
}

std::string statusPageUrl(int port) { return "http://127.0.0.1:" + std::to_string(port) + "/"; }

std::vector<MenuItem> buildStatusMenu(const MenuInput& input) {
  std::vector<MenuItem> items;
  items.push_back(info(input.version.empty() ? "AWTRIX Script Host" : "AWTRIX Script Host " + input.version));
  items.push_back(info(statusLine(input.host, input.port)));
  if (input.host.state == HostState::Stopped &&
      (input.host.reason == StopReason::PortBusy || input.host.reason == StopReason::CannotStart))
    items.push_back(command("Start again", MenuAction::Start));
  items.push_back(separator());

  Status status;
  if (input.host.up() && readStatus(input.status, status)) {
    if (!status.code.empty())
      items.push_back(command("Pairing code " + status.code, MenuAction::CopyCode, status.code));
    if (status.clocksKnown && status.clocks.empty()) items.push_back(info("No clock paired yet"));
    for (MenuItem& clock : status.clocks) items.push_back(std::move(clock));
    if (!items.back().separator) items.push_back(separator());
  }

  items.push_back(
      command("Open status page", MenuAction::OpenStatusPage, statusPageUrl(input.port), input.host.up()));
  items.push_back(command("Open log", MenuAction::OpenLog, input.logPath, !input.logPath.empty()));
  MenuItem login = command("Start at login", MenuAction::StartAtLogin);
  login.checkable = true;
  login.checked = input.startAtLogin;
  items.push_back(std::move(login));
  items.push_back(separator());
  items.push_back(command("Quit", MenuAction::Quit));
  return items;
}

}
