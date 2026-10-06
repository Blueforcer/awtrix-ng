#include "platform/tc002/daemon/Autostart.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstring>

#include "platform/posix/Files.h"
#include "platform/tc002/daemon/Log.h"
#include "platform/tc002/daemon/Process.h"

namespace awtrix {
namespace tc002d {
namespace {

constexpr char kComponent[] = "autostart";
constexpr std::size_t kMaxOutputLine = 512;

}

AutostartService::AutostartService(AutostartOptions options) : options_(std::move(options)) {
  if (options_.backoffMs.empty()) options_.backoffMs.push_back(1000);
  if (options_.maxFailures == 0) options_.maxFailures = 1;
}

AutostartService::~AutostartService() {
  if (pid_ <= 0) return;
  signalGroup(SIGKILL);
  reapUntil(pid_, posix::monotonicMs() + kReapGraceMs);
}

const char* AutostartService::stateName(State state) {
  switch (state) {
    case State::None: return "none";
    case State::Refused: return "refused";
    case State::Waiting: return "waiting";
    case State::Running: return "running";
    case State::Stopping: return "stopping";
    case State::Finished: return "finished";
    case State::GaveUp: return "gave up";
  }
  return "none";
}

std::string AutostartService::inspect(bool& present) const {
  struct stat info{};
  present = ::lstat(options_.path.c_str(), &info) == 0;
  if (!present) return errno == ENOENT ? std::string() : std::string("cannot inspect it: ") + std::strerror(errno);
  if (!S_ISREG(info.st_mode)) return "not a regular file";
  if (info.st_uid != options_.owner) return "owned by uid " + std::to_string(info.st_uid) + ", not root";
  if (info.st_mode & (S_IWGRP | S_IWOTH)) return "writable by group or others";
  if (!(info.st_mode & S_IXUSR)) return "not executable";
  return {};
}

bool AutostartService::start(int64_t nowMs) {
  stopping_ = false;
  windowStart_ = nowMs;
  launch(nowMs);
  return true;
}

void AutostartService::launch(int64_t nowMs) {
  launchAt_ = -1;
  bool present = false;
  refusal_ = inspect(present);
  if (!present && refusal_.empty()) {
    if (state_ != State::None) Log::line(kComponent, "%s is gone; nothing to start", options_.path.c_str());
    state_ = State::None;
    return;
  }
  if (!refusal_.empty()) {
    Log::line(kComponent, "not starting %s: %s", options_.path.c_str(), refusal_.c_str());
    state_ = State::Refused;
    return;
  }
  if (output_.valid()) {
    readOutput(true, nowMs);
    output_.reset();
  }
  int output[2];
  if (::pipe2(output, O_CLOEXEC) < 0) {
    lastExit_ = std::string("pipe: ") + std::strerror(errno);
    Log::line(kComponent, "cannot start %s: %s", options_.path.c_str(), lastExit_.c_str());
    onChildExit(-1, -1, nowMs);
    return;
  }
  posix::UniqueFd readEnd(output[0]), writeEnd(output[1]);
  ::fcntl(readEnd.get(), F_SETFL, O_NONBLOCK);
  ProcessSpec spec;
  spec.path = options_.path;
  spec.argv = {options_.path};
  spec.environment = options_.environment;
  spec.descriptors = {{writeEnd.get(), STDOUT_FILENO}, {writeEnd.get(), STDERR_FILENO}};
  spec.group = ProcessGroup::Session;
  spec.parentDeathSignal = SIGKILL;
  spec.umask = 022;
  std::string error;
  const pid_t pid = spawnProcess(spec, error);
  ++starts_;
  startedAt_ = nowMs;
  if (pid < 0) {
    lastExit_ = error;
    Log::line(kComponent, "cannot start %s: %s", options_.path.c_str(), error.c_str());
    onChildExit(-1, -1, nowMs);
    return;
  }
  pid_ = pid;
  output_ = std::move(readEnd);
  buffer_.clear();
  state_ = State::Running;
  Log::line(kComponent, "started %s as pid %d (start %u)", options_.path.c_str(), static_cast<int>(pid), starts_);
}

void AutostartService::signalGroup(int signal) {
  if (pid_ <= 0) return;
  if (::kill(-pid_, signal) < 0) ::kill(pid_, signal);
}

void AutostartService::pollInterest(std::vector<PollInterest>& out) const {
  if (output_.valid()) out.push_back({output_.get(), POLLIN});
}

void AutostartService::onReady(int fd, short revents, int64_t nowMs) {
  (void)revents;
  if (fd >= 0 && fd == output_.get()) readOutput(false, nowMs);
}

int64_t AutostartService::nextDeadlineMs() const {
  int64_t next = killAt_;
  if (state_ == State::Waiting && launchAt_ >= 0 && (next < 0 || launchAt_ < next)) next = launchAt_;
  return next;
}

void AutostartService::onTime(int64_t nowMs) {
  if (killAt_ >= 0 && nowMs >= killAt_) {
    killAt_ = -1;
    if (pid_ > 0) {
      Log::line(kComponent, "pid %d ignored SIGTERM; killing it", static_cast<int>(pid_));
      signalGroup(SIGKILL);
    }
  }
  if (state_ == State::Waiting && !stopping_ && launchAt_ >= 0 && nowMs >= launchAt_) launch(nowMs);
}

bool AutostartService::onChildExit(pid_t pid, int status, int64_t nowMs) {
  const bool spawnFailed = pid < 0;
  if (!spawnFailed && (pid_ <= 0 || pid != pid_)) return false;
  if (!spawnFailed) {
    readOutput(false, nowMs);
    lastExit_ = describeWait(status);
    Log::line(kComponent, "pid %d ended: %s", static_cast<int>(pid), lastExit_.c_str());
    pid_ = -1;
    killAt_ = -1;
  }
  if (suppressed_) closeWindow(nowMs);
  if (restartPending_) {
    restartPending_ = false;
    failures_ = 0;
    if (!stopping_) launch(nowMs);
    return true;
  }
  if (stopping_) return true;
  if (!spawnFailed && WIFEXITED(status) && WEXITSTATUS(status) == 0) {
    failures_ = 0;
    state_ = State::Finished;
    return true;
  }
  if (!spawnFailed && nowMs - startedAt_ >= options_.stableMs) failures_ = 0;
  if (++failures_ >= options_.maxFailures) {
    state_ = State::GaveUp;
    Log::line(kComponent, "%u failures in a row; not starting it again until the daemon restarts", failures_);
    return true;
  }
  const std::size_t step = std::min<std::size_t>(failures_ - 1, options_.backoffMs.size() - 1);
  state_ = State::Waiting;
  launchAt_ = nowMs + options_.backoffMs[step];
  Log::line(kComponent, "starting it again in %lld ms", static_cast<long long>(options_.backoffMs[step]));
  return true;
}

void AutostartService::requestStop(int64_t nowMs) {
  stopping_ = true;
  restartPending_ = false;
  launchAt_ = -1;
  if (pid_ <= 0) return;
  state_ = State::Stopping;
  signalGroup(SIGTERM);
  killAt_ = nowMs + options_.stopGraceMs;
}

void AutostartService::restart(int64_t nowMs) {
  if (stopping_) return;
  if (pid_ > 0) {
    Log::line(kComponent, "restart requested; stopping pid %d", static_cast<int>(pid_));
    restartPending_ = true;
    state_ = State::Stopping;
    signalGroup(SIGTERM);
    killAt_ = nowMs + options_.stopGraceMs;
    return;
  }
  Log::line(kComponent, "restart requested");
  failures_ = 0;
  launch(nowMs);
}

void AutostartService::readOutput(bool final, int64_t nowMs) {
  char chunk[2048];
  for (int round = 0; (final || round < 8) && output_.valid(); ++round) {
    const ssize_t count = ::read(output_.get(), chunk, sizeof chunk);
    if (count < 0 && errno == EINTR) continue;
    if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
    if (count <= 0) {
      output_.reset();
      final = true;
      break;
    }
    buffer_.append(chunk, static_cast<std::size_t>(count));
    std::size_t begin = 0;
    for (;;) {
      const auto newline = buffer_.find('\n', begin);
      if (newline == std::string::npos) break;
      emit(std::string_view(buffer_).substr(begin, newline - begin), nowMs);
      begin = newline + 1;
    }
    buffer_.erase(0, begin);
    if (buffer_.size() > kMaxOutputLine) {
      emit(buffer_, nowMs);
      buffer_.clear();
    }
  }
  if (final && !buffer_.empty()) {
    emit(buffer_, nowMs);
    buffer_.clear();
  }
}

void AutostartService::emit(std::string_view text, int64_t nowMs) {
  if (nowMs - windowStart_ >= options_.logWindowMs) closeWindow(nowMs);
  if (windowLines_ < options_.logBurst) {
    ++windowLines_;
    Log::text(kComponent, text.substr(0, kMaxOutputLine));
  } else {
    ++suppressed_;
  }
}

void AutostartService::closeWindow(int64_t nowMs) {
  if (suppressed_) Log::line(kComponent, "%u output lines not logged", suppressed_);
  windowStart_ = nowMs;
  windowLines_ = 0;
  suppressed_ = 0;
}

void AutostartService::appendStatus(api::JsonWriter& json, int64_t nowMs) const {
  json.key("autostart").beginObject()
      .member("path", options_.path)
      .member("state", stateName(state_))
      .member("pid", static_cast<int>(pid_))
      .member("starts", starts_)
      .member("consecutiveFailures", failures_)
      .member("lastExit", lastExit_)
      .member("refusal", refusal_);
  if (pid_ > 0) json.member("uptimeMs", static_cast<long long>(nowMs - startedAt_));
  if (state_ == State::Waiting && launchAt_ >= 0)
    json.member("nextStartInMs", static_cast<long long>(launchAt_ > nowMs ? launchAt_ - nowMs : 0));
  json.endObject();
}

}
}
