#include "platform/host/Process.h"

#include <fcntl.h>
#include <spawn.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <crt_externs.h>
#else
extern char** environ;
#endif

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <thread>
#include <utility>

namespace awtrix::os {
namespace {

std::atomic<bool>* g_stop = nullptr;

void requested(int) {
  if (g_stop) g_stop->store(true);
}

int exitStatus(int code) { return WIFEXITED(code) ? WEXITSTATUS(code) : 128 + WTERMSIG(code); }

char** environment() {
#if defined(__APPLE__)
  return *_NSGetEnviron();
#else
  return environ;
#endif
}

}

ChildProcess::ChildProcess(ChildProcess&& other) noexcept : id_(std::exchange(other.id_, -1)) {}

ChildProcess& ChildProcess::operator=(ChildProcess&& other) noexcept {
  if (this != &other) {
    stop();
    id_ = std::exchange(other.id_, -1);
  }
  return *this;
}

bool ChildProcess::start(const std::vector<std::string>& args, const std::string& log) {
  if (args.empty() || running()) return false;
  std::vector<std::string> copies = args;
  std::vector<char*> argv;
  for (std::string& arg : copies) argv.push_back(arg.data());
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
  id_ = pid;
  return true;
}

bool ChildProcess::ended(int& status) {
  if (id_ <= 0) return false;
  int code = 0;
  if (::waitpid(static_cast<pid_t>(id_), &code, WNOHANG) != id_) return false;
  status = exitStatus(code);
  id_ = -1;
  return true;
}

void ChildProcess::stop(int graceMs) {
  if (id_ <= 0) return;
  const pid_t pid = static_cast<pid_t>(std::exchange(id_, -1));
  ::kill(pid, SIGTERM);
  for (int waited = 0; waited < graceMs; waited += 50) {
    if (::waitpid(pid, nullptr, WNOHANG) == pid) return;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  ::kill(pid, SIGKILL);
  ::waitpid(pid, nullptr, 0);
}

bool TiedProcess::start(const std::vector<std::string>& args) {
  if (args.empty() || running()) return false;
  int ends[2];
  if (::pipe(ends) != 0) return false;
  if (ends[0] != STDIN_FILENO) ::fcntl(ends[0], F_SETFD, FD_CLOEXEC);
  ::fcntl(ends[1], F_SETFD, FD_CLOEXEC);
  std::vector<std::string> copies = args;
  std::vector<char*> argv;
  for (std::string& arg : copies) argv.push_back(arg.data());
  argv.push_back(nullptr);
  posix_spawn_file_actions_t actions;
  ::posix_spawn_file_actions_init(&actions);
  ::posix_spawn_file_actions_adddup2(&actions, ends[0], STDIN_FILENO);
  ::posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
  ::posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);
  pid_t pid = -1;
  const int error = ::posix_spawn(&pid, argv[0], &actions, nullptr, argv.data(), environment());
  ::posix_spawn_file_actions_destroy(&actions);
  ::close(ends[0]);
  if (error != 0) {
    ::close(ends[1]);
    return false;
  }
  id_ = pid;
  input_ = ends[1];
  return true;
}

bool TiedProcess::ended(int& status) {
  if (id_ <= 0) return false;
  int code = 0;
  const pid_t reaped = ::waitpid(static_cast<pid_t>(id_), &code, WNOHANG);
  if (reaped == 0 || (reaped < 0 && errno == EINTR)) return false;
  status = reaped < 0 ? -1 : exitStatus(code);
  id_ = -1;
  closeInput();
  return true;
}

bool TiedProcess::wait(int timeoutMs, int& status) {
  for (int waited = 0;; waited += 10) {
    if (ended(status)) return true;
    if (!running() || waited >= timeoutMs) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

void TiedProcess::closeInput() {
  if (input_ >= 0) ::close(static_cast<int>(std::exchange(input_, -1)));
}

void TiedProcess::stop(int graceMs) {
  closeInput();
  int status = 0;
  if (!running() || wait(graceMs, status)) return;
  const pid_t pid = static_cast<pid_t>(std::exchange(id_, -1));
  ::kill(pid, SIGKILL);
  ::waitpid(pid, nullptr, 0);
}

void stopOnRequest(std::atomic<bool>& stop) {
  g_stop = &stop;
  std::signal(SIGPIPE, SIG_IGN);
  std::signal(SIGINT, requested);
  std::signal(SIGTERM, requested);
}

void stopAtInputEnd(std::atomic<bool>& stop) {
  std::thread([&stop] {
    char buffer[256];
    for (;;) {
      const ssize_t count = ::read(STDIN_FILENO, buffer, sizeof buffer);
      if (count <= 0 && !(count < 0 && errno == EINTR)) break;
    }
    stop.store(true);
  }).detach();
}

void lowerThreadPriority(int nice) { ::setpriority(PRIO_PROCESS, 0, nice); }

std::string hostName() {
  char name[256] = {};
  if (::gethostname(name, sizeof(name) - 1) != 0 || !name[0]) return {};
  return name;
}

}
