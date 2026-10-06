#include "platform/tc002/daemon/EventLoop.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstring>

#include "platform/posix/Files.h"
#include "platform/tc002/daemon/Log.h"

namespace awtrix {
namespace tc002d {
namespace {

int gSignalWrite = -1;

void onSignal(int signal) {
  const int saved = errno;
  const unsigned char code = static_cast<unsigned char>(signal);
  if (gSignalWrite >= 0) (void)!::write(gSignalWrite, &code, 1);
  errno = saved;
}

void install(int signal, void (*handler)(int)) {
  struct sigaction action{};
  action.sa_handler = handler;
  sigemptyset(&action.sa_mask);
  action.sa_flags = SA_RESTART | (signal == SIGCHLD ? SA_NOCLDSTOP : 0);
  sigaction(signal, &action, nullptr);
}

std::string describeStatus(int status) {
  char text[48];
  if (WIFEXITED(status)) std::snprintf(text, sizeof text, "exit %d", WEXITSTATUS(status));
  else if (WIFSIGNALED(status)) std::snprintf(text, sizeof text, "signal %d", WTERMSIG(status));
  else std::snprintf(text, sizeof text, "status 0x%x", static_cast<unsigned>(status));
  return text;
}

}

EventLoop::EventLoop() {
  int fds[2];
  if (::pipe2(fds, O_CLOEXEC | O_NONBLOCK) == 0) {
    signalRead_ = fds[0];
    gSignalWrite = fds[1];
  }
  install(SIGCHLD, onSignal);
  install(SIGTERM, onSignal);
  install(SIGINT, onSignal);
  install(SIGHUP, onSignal);
  install(SIGPIPE, SIG_IGN);
  sigset_t unblock;
  sigemptyset(&unblock);
  for (int signal : {SIGCHLD, SIGTERM, SIGINT, SIGHUP}) sigaddset(&unblock, signal);
  sigprocmask(SIG_UNBLOCK, &unblock, nullptr);
}

EventLoop::~EventLoop() {
  for (int signal : {SIGCHLD, SIGTERM, SIGINT, SIGHUP}) install(signal, SIG_DFL);
  if (gSignalWrite >= 0) ::close(gSignalWrite);
  gSignalWrite = -1;
  if (signalRead_ >= 0) ::close(signalRead_);
}

void EventLoop::add(Service& service, int64_t stopTimeoutMs) {
  Entry entry;
  entry.service = &service;
  entry.stopTimeoutMs = stopTimeoutMs;
  entries_.push_back(entry);
}

bool EventLoop::start() {
  const int64_t now = posix::monotonicMs();
  for (auto& entry : entries_) {
    entry.started = true;
    stopCursor_ = static_cast<std::size_t>(&entry - entries_.data()) + 1;
    if (!entry.service->start(now)) {
      Log::line("daemon", "service %s failed to start", entry.service->name());
      requestStop(std::string("start of ") + entry.service->name() + " failed");
      return false;
    }
    Log::line("daemon", "service %s started", entry.service->name());
  }
  return true;
}

void EventLoop::requestStop(const std::string& reason) {
  if (stopping_) return;
  stopping_ = true;
  stopReason_ = reason;
  Log::line("daemon", "stopping: %s", reason.c_str());
}

void EventLoop::drainSignals(int64_t nowMs) {
  unsigned char codes[64];
  bool child = false;
  for (;;) {
    const ssize_t count = ::read(signalRead_, codes, sizeof codes);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) break;
    for (ssize_t i = 0; i < count; ++i) {
      const int signal = codes[i];
      if (signal == SIGCHLD) child = true;
      else if (signal == SIGHUP) Log::line("daemon", "SIGHUP ignored");
      else requestStop(std::string("signal ") + strsignal(signal));
    }
  }
  if (child) reapChildren(nowMs);
}

void EventLoop::reapChildren(int64_t nowMs) {
  for (;;) {
    int status = 0;
    const pid_t pid = ::waitpid(-1, &status, WNOHANG);
    if (pid <= 0) return;
    bool claimed = false;
    for (auto& entry : entries_) {
      if (entry.started && entry.service->onChildExit(pid, status, nowMs)) {
        claimed = true;
        break;
      }
    }
    if (!claimed) {
      ++unclaimedChildren_;
      Log::line("daemon", "reaped unclaimed pid %d (%s)", static_cast<int>(pid), describeStatus(status).c_str());
    }
  }
}

void EventLoop::advanceStop(int64_t nowMs) {
  while (stopCursor_ > 0) {
    Entry& entry = entries_[stopCursor_ - 1];
    if (!entry.started) {
      --stopCursor_;
      continue;
    }
    if (!entry.stopRequested) {
      entry.stopRequested = true;
      entry.stopDeadline = nowMs + entry.stopTimeoutMs;
      entry.service->requestStop(nowMs);
    }
    if (entry.service->stopped()) {
      Log::line("daemon", "service %s stopped", entry.service->name());
      --stopCursor_;
      continue;
    }
    if (nowMs >= entry.stopDeadline) {
      Log::line("daemon", "service %s did not stop within %lld ms", entry.service->name(),
                static_cast<long long>(entry.stopTimeoutMs));
      --stopCursor_;
      continue;
    }
    return;
  }
  finished_ = true;
}

bool EventLoop::runOnce(int maxWaitMs) {
  if (finished_) return false;
  int64_t now = posix::monotonicMs();
  if (stopping_) {
    advanceStop(now);
    if (finished_) return false;
  }
  std::vector<pollfd> fds;
  std::vector<Service*> owners;
  std::vector<PollInterest> interest;
  fds.push_back({signalRead_, POLLIN, 0});
  owners.push_back(nullptr);
  int64_t deadline = now + (maxWaitMs < 0 ? 0 : maxWaitMs);
  for (auto& entry : entries_) {
    if (!entry.started) continue;
    interest.clear();
    entry.service->pollInterest(interest);
    for (const auto& item : interest) {
      if (item.fd < 0) continue;
      fds.push_back({item.fd, item.events, 0});
      owners.push_back(entry.service);
    }
    const int64_t due = entry.service->nextDeadlineMs();
    if (due >= 0 && due < deadline) deadline = due;
  }
  if (stopping_ && stopCursor_ > 0) {
    const Entry& current = entries_[stopCursor_ - 1];
    if (current.stopRequested && current.stopDeadline < deadline) deadline = current.stopDeadline;
  }
  const int64_t wait = deadline > now ? deadline - now : 0;
  const int ready = ::poll(fds.data(), fds.size(), static_cast<int>(wait));
  now = posix::monotonicMs();
  if (ready < 0 && errno != EINTR) Log::line("daemon", "poll failed: %s", std::strerror(errno));
  drainSignals(now);
  if (ready > 0) {
    for (std::size_t i = 1; i < fds.size(); ++i) {
      if (fds[i].revents) owners[i]->onReady(fds[i].fd, fds[i].revents, now);
    }
  }
  now = posix::monotonicMs();
  for (auto& entry : entries_) {
    if (!entry.started) continue;
    const int64_t due = entry.service->nextDeadlineMs();
    if (due >= 0 && due <= now) entry.service->onTime(now);
  }
  if (stopping_) advanceStop(posix::monotonicMs());
  return !finished_;
}

void EventLoop::run() {
  while (runOnce(1000)) {}
}

}
}
