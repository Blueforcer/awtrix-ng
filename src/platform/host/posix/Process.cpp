#include "platform/host/Process.h"

#include <fcntl.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

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
  status = WIFEXITED(code) ? WEXITSTATUS(code) : 128 + WTERMSIG(code);
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

void stopOnRequest(std::atomic<bool>& stop) {
  g_stop = &stop;
  std::signal(SIGPIPE, SIG_IGN);
  std::signal(SIGINT, requested);
  std::signal(SIGTERM, requested);
}

void lowerThreadPriority(int nice) { ::setpriority(PRIO_PROCESS, 0, nice); }

std::string hostName() {
  char name[256] = {};
  if (::gethostname(name, sizeof(name) - 1) != 0 || !name[0]) return {};
  return name;
}

}
