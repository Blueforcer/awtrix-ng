#include "platform/host/app/win32/TrayController.h"

#include <algorithm>
#include <utility>

namespace awtrix::hostapp::win32 {

TrayController::TrayController(WatchedProcess& process, std::vector<std::string> args, TrayView& view, Shell& shell,
                               StartAtLogin& login, StatusFetch fetch, TrayFacts facts)
    : process_(process),
      supervisor_(process, std::move(args)),
      view_(view),
      shell_(shell),
      login_(login),
      fetch_(std::move(fetch)),
      facts_(std::move(facts)) {}

void TrayController::begin(int64_t nowMs) {
  supervisor_.start(nowMs);
  refresh(nowMs);
}

void TrayController::hostSignalled(int64_t nowMs) {
  int exitCode = 0;
  if (process_.ended(exitCode)) supervisor_.ended(exitCode, nowMs);
  refresh(nowMs);
}

void TrayController::wake(int64_t nowMs) {
  supervisor_.wake(nowMs);
  refresh(nowMs);
}

std::string TrayController::tip(int64_t nowMs) const {
  return "AWTRIX Script Host\n" + statusLine(supervisor_.view(nowMs), facts_.port);
}

std::vector<MenuItem> TrayController::menu(int64_t nowMs) {
  MenuInput input;
  input.version = facts_.version;
  input.port = facts_.port;
  input.host = supervisor_.view(nowMs);
  std::string status;
  if (input.host.up() && fetch_(facts_.port, status)) input.status = status;
  if (!facts_.logPath.empty() && shell_.exists(facts_.logPath)) input.logPath = facts_.logPath;
  input.startAtLogin = login_.enabled();
  return buildStatusMenu(input);
}

bool TrayController::choose(const MenuItem& item, int64_t nowMs) {
  if (!item.enabled || quit_) return !quit_;
  switch (item.action) {
    case MenuAction::Start:
      supervisor_.start(nowMs);
      refresh(nowMs);
      break;
    case MenuAction::CopyCode:
      shell_.copy(item.value);
      break;
    case MenuAction::OpenClock:
    case MenuAction::OpenStatusPage:
    case MenuAction::OpenLog:
      shell_.open(item.value);
      break;
    case MenuAction::StartAtLogin:
      login_.set(!item.checked);
      break;
    case MenuAction::Quit:
      quit();
      return false;
    case MenuAction::None:
      break;
  }
  return true;
}

void TrayController::quit() {
  if (quit_) return;
  quit_ = true;
  view_.wakeIn(std::nullopt);
  view_.hide();
  supervisor_.quit();
}

void TrayController::refresh(int64_t nowMs) {
  if (quit_) return;
  view_.show(supervisor_.state(), tip(nowMs));
  const std::optional<int64_t> at = supervisor_.wakeAt();
  view_.wakeIn(at ? std::optional<int64_t>(std::max<int64_t>(0, *at - nowMs)) : std::nullopt);
}

}
