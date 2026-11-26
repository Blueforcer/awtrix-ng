#include "platform/linux/hostd/Children.h"

#include <csignal>
#include <cstdio>
#include <thread>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace awtrix::hostd {

bool Children::launch(const ClockStore& store, const ClockRecord& clock, Child& child, int64_t nowMs) {
  const std::string data = store.dataDir(clock.uid).string();
  const std::string log = (store.clockDir(clock.uid) / "runtime.log").string();
  std::vector<std::string> args = {binary_, "--data", data, "--port", std::to_string(clock.port),
                                   "--width", std::to_string(clock.width), "--height", std::to_string(clock.height),
                                   "--remote", "--remote-clock", clock.uid,
                                   "--remote-key-file", store.keyFile(clock.uid).string(),
                                   "--remote-clock-url", clock.clockUrl};
  if (!webui_.empty()) {
    args.push_back("--webui");
    args.push_back(webui_);
  }
  std::vector<char*> argv;
  for (std::string& arg : args) argv.push_back(arg.data());
  argv.push_back(nullptr);
  const pid_t pid = ::fork();
  if (pid < 0) return false;
  if (pid == 0) {
    const int out = ::open(log.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
    if (out >= 0) {
      ::dup2(out, STDOUT_FILENO);
      ::dup2(out, STDERR_FILENO);
    }
    ::execv(argv[0], argv.data());
    std::perror("exec");
    ::_exit(127);
  }
  child.pid = pid;
  child.startedMs = nowMs;
  std::printf("clock %s: process %d on port %d\n", clock.uid.c_str(), static_cast<int>(pid), clock.port);
  std::fflush(stdout);
  return true;
}

void Children::terminate(pid_t pid) {
  if (pid <= 0) return;
  ::kill(pid, SIGTERM);
  for (int i = 0; i < 100; ++i) {
    if (::waitpid(pid, nullptr, WNOHANG) == pid) return;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  ::kill(pid, SIGKILL);
  ::waitpid(pid, nullptr, 0);
}

void Children::sync(const ClockStore& store, int64_t nowMs) {
  for (auto it = children_.begin(); it != children_.end();) {
    if (store.find(it->first)) {
      ++it;
      continue;
    }
    terminate(it->second.pid);
    it = children_.erase(it);
  }
  for (const ClockRecord& clock : store.clocks()) {
    Child& child = children_[clock.uid];
    if (child.pid < 0 && child.restartAtMs <= nowMs) launch(store, clock, child, nowMs);
  }
}

void Children::restart(const std::string& uid) {
  const auto it = children_.find(uid);
  if (it == children_.end()) return;
  terminate(it->second.pid);
  it->second = Child{};
}

void Children::stop(const std::string& uid) {
  const auto it = children_.find(uid);
  if (it == children_.end()) return;
  terminate(it->second.pid);
  children_.erase(it);
}

void Children::stopAll() {
  for (auto& entry : children_) terminate(entry.second.pid);
  children_.clear();
}

void Children::tick(const ClockStore& store, int64_t nowMs) {
  for (auto& entry : children_) {
    Child& child = entry.second;
    if (child.pid > 0) {
      int status = 0;
      if (::waitpid(child.pid, &status, WNOHANG) != child.pid) continue;
      const bool healthy = nowMs - child.startedMs >= kHealthyMs;
      child.backoffMs = healthy ? kFirstBackoffMs : std::min(child.backoffMs * 2, kMaxBackoffMs);
      child.restartAtMs = nowMs + (healthy ? kFirstBackoffMs : child.backoffMs);
      std::printf("clock %s: process %d ended (status %d), again in %lld ms\n", entry.first.c_str(),
                  static_cast<int>(child.pid), status, static_cast<long long>(child.restartAtMs - nowMs));
      std::fflush(stdout);
      child.pid = -1;
      continue;
    }
    if (nowMs < child.restartAtMs) continue;
    const ClockRecord* clock = store.find(entry.first);
    if (clock) launch(store, *clock, child, nowMs);
  }
}

bool Children::running(const std::string& uid) const {
  const auto it = children_.find(uid);
  return it != children_.end() && it->second.pid > 0;
}

}
