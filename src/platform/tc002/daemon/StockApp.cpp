#include "platform/tc002/daemon/StockApp.h"

#include <dirent.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <climits>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "platform/posix/Files.h"
#include "platform/tc002/daemon/Log.h"
#include "platform/tc002/daemon/PropertyWorkspace.h"

namespace awtrix {
namespace tc002d {
namespace {

constexpr char kName[] = "hciattach";
constexpr char kExecutable[] = "/res/bin/hciattach";

bool sameExecutable(const std::string& target, const char* executable) {
  return target == executable || target == std::string(executable) + " (deleted)";
}

bool zombie(const std::string& process) {
  std::string stat;
  if (!posix::readText(process + "/stat", stat, 1024)) return false;
  const auto close = stat.rfind(") ");
  return close != std::string::npos && close + 2 < stat.size() && stat[close + 2] == 'Z';
}

// Processes named comm, or running executable under another name; exited ones awaiting their
// parent's wait are not counted.
bool findProcesses(const std::string& procRoot, const char* comm, const char* executable, std::vector<pid_t>& out) {
  out.clear();
  std::unique_ptr<DIR, int (*)(DIR*)> processes(opendir(procRoot.c_str()), closedir);
  if (!processes) return false;
  for (;;) {
    errno = 0;
    const dirent* entry = readdir(processes.get());
    if (!entry) return errno == 0;
    if (!posix::numeric(entry->d_name)) continue;
    const std::string process = procRoot + "/" + entry->d_name;
    std::string name;
    bool match = posix::readText(process + "/comm", name, 64) && posix::trimmed(name) == comm;
    if (!match) {
      char target[PATH_MAX];
      const ssize_t length = ::readlink((process + "/exe").c_str(), target, sizeof target - 1);
      match = length > 0 && sameExecutable(std::string(target, static_cast<std::size_t>(length)), executable);
    }
    if (match && !zombie(process)) out.push_back(static_cast<pid_t>(std::atol(entry->d_name)));
  }
}

// Starts "<setprop> <key> <value>" with the inherited property workspace and returns its pid
// without waiting; -1 when there is no workspace or the start failed.
pid_t spawnSetprop(const std::string& setprop, const std::string& key, const std::string& value) {
  ProcessSpec spec;
  std::string error;
  if (!PropertyWorkspace::setprop(setprop, key, value, spec)) return -1;
  return spawnProcess(spec, error);
}

std::string pidList(const std::vector<pid_t>& pids) {
  std::string text = pids.size() == 1 ? "pid " : "pids ";
  for (std::size_t i = 0; i < pids.size(); ++i) text += (i ? "," : "") + std::to_string(pids[i]);
  return text;
}

void killAll(const std::vector<pid_t>& pids) {
  for (const pid_t pid : pids) ::kill(pid, SIGKILL);
}

}

StockWatch::StockWatch(StockWatchOptions options) : options_(std::move(options)) {
  if (options_.intervalMs <= 0) options_.intervalMs = 5000;
}

bool StockWatch::start(int64_t nowMs) {
  stopping_ = false;
  nextCheck_ = nowMs;
  Log::line("stock", "keeping %s off; checking every %lld ms", kName, static_cast<long long>(options_.intervalMs));
  if (!PropertyWorkspace::available())
    Log::line("stock", "no property workspace: a stock process that appears is killed without ctl.stop");
  return true;
}

void StockWatch::onTime(int64_t nowMs) {
  if (stopping_ || nowMs < nextCheck_) return;
  nextCheck_ = nowMs + options_.intervalMs;
  ++checks_;
  check();
}

void StockWatch::check() {
  std::vector<pid_t> pids;
  if (!findProcesses(options_.procRoot, kName, kExecutable, pids)) {
    if (!blind_) Log::line("stock", "cannot read %s: %s", options_.procRoot.c_str(), std::strerror(errno));
    blind_ = true;
    return;
  }
  if (blind_) Log::line("stock", "%s readable again", options_.procRoot.c_str());
  blind_ = false;
  if (setprop_ > 0) {
    Log::line("stock", "setprop ctl.stop %s pid %d did not finish; killing it", kName, static_cast<int>(setprop_));
    ::kill(setprop_, SIGKILL);
  }
  if (pids.empty()) {
    if (phase_ == Phase::Stopping) Log::line("stock", "%s stopped", kName);
    else if (phase_ != Phase::Off) Log::line("stock", "%s gone after SIGKILL", kName);
    phase_ = Phase::Off;
    seen_.clear();
    return;
  }
  bool fresh = phase_ == Phase::Off;
  for (const pid_t pid : pids)
    if (std::find(seen_.begin(), seen_.end(), pid) == seen_.end()) fresh = true;
  seen_ = pids;
  if (fresh) {
    stopAppeared(pids);
    return;
  }
  switch (phase_) {
    case Phase::Off:
      return;
    case Phase::Stopping:
      Log::line("stock", "%s %s survived ctl.stop %s; sending SIGKILL", kName, pidList(pids).c_str(), kName);
      killAll(pids);
      phase_ = Phase::Killed;
      return;
    case Phase::Killed:
      Log::line("stock", "%s %s survived SIGKILL; retrying at every check", kName, pidList(pids).c_str());
      killAll(pids);
      phase_ = Phase::Stuck;
      return;
    case Phase::Stuck:
      killAll(pids);
      return;
  }
}

void StockWatch::stopAppeared(const std::vector<pid_t>& pids) {
  if (setprop_ <= 0) {
    const pid_t pid = spawnSetprop(options_.setprop, "ctl.stop", kName);
    if (pid > 0) {
      setprop_ = pid;
      phase_ = Phase::Stopping;
      Log::line("stock", "%s running (%s); sent ctl.stop %s", kName, pidList(pids).c_str(), kName);
      return;
    }
  }
  Log::line("stock", "%s running (%s); ctl.stop %s unavailable, sending SIGKILL", kName, pidList(pids).c_str(),
            kName);
  killAll(pids);
  phase_ = Phase::Killed;
}

bool StockWatch::onChildExit(pid_t pid, int status, int64_t nowMs) {
  (void)nowMs;
  if (setprop_ <= 0 || pid != setprop_) return false;
  setprop_ = -1;
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
    Log::line("stock", "setprop ctl.stop %s failed (%s)", kName, describeWait(status).c_str());
  return true;
}

void StockWatch::requestStop(int64_t nowMs) {
  (void)nowMs;
  stopping_ = true;
}

}
}
