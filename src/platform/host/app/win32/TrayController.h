#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "platform/host/app/HostSupervisor.h"
#include "platform/host/app/StatusMenu.h"
#include "platform/host/app/win32/StartAtLogin.h"
#include "platform/host/app/win32/WatchedProcess.h"
#include "platform/linux/hostd/Contract.h"

namespace awtrix::hostapp::win32 {

class TrayView {
 public:
  virtual ~TrayView() = default;
  virtual void show(HostState state, const std::string& tip) = 0;
  virtual void wakeIn(std::optional<int64_t> delayMs) = 0;
  virtual void hide() = 0;
};

class Shell {
 public:
  virtual ~Shell() = default;
  virtual bool open(const std::string& target) = 0;
  virtual bool copy(const std::string& text) = 0;
  virtual bool exists(const std::string& path) = 0;
};

using StatusFetch = std::function<bool(int port, std::string& body)>;

struct TrayFacts {
  std::string version;
  int port = hostd::kDefaultPort;
  std::string logPath;
};

class TrayController {
 public:
  TrayController(WatchedProcess& process, std::vector<std::string> args, TrayView& view, Shell& shell,
                 StartAtLogin& login, StatusFetch fetch, TrayFacts facts);

  void begin(int64_t nowMs);
  void hostSignalled(int64_t nowMs);
  void wake(int64_t nowMs);
  std::string tip(int64_t nowMs) const;
  std::vector<MenuItem> menu(int64_t nowMs);
  bool choose(const MenuItem& item, int64_t nowMs);
  void quit();

 private:
  void refresh(int64_t nowMs);

  WatchedProcess& process_;
  HostSupervisor supervisor_;
  TrayView& view_;
  Shell& shell_;
  StartAtLogin& login_;
  StatusFetch fetch_;
  TrayFacts facts_;
  bool quit_ = false;
};

}
