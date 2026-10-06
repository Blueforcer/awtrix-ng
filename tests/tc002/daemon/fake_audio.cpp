// Stand-in for awtrix-tc002-audio-pcm used by test_tc002d_runtime and test_tc002d_speaker. It
// checks the helper launch contract (argv, empty environment, SOCK_SEQPACKET on 104), holds an
// exclusive lock next to its own path the way the real helper holds /dev/awtrix_pcm, reports
// "helper ok" or the problem over the socket, and then behaves according to
// <dir of argv[0]>/helper-mode:
//   ok         exit 0 as soon as the socket closes
//   slow       stay 500 ms after the socket closes (a successor must wait for the reap)
//   linger     after the socket closes ignore SIGTERM and wait to be killed
//   preflight  exit 3 right away
// Any process of the same user may ptrace it, so a test can hold it unreapable.
#include <fcntl.h>
#include <poll.h>
#include <sys/file.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>

extern char** environ;

namespace {

std::string directoryOf(const char* path) {
  const std::string text(path);
  const auto slash = text.rfind('/');
  return slash == std::string::npos ? "." : text.substr(0, slash);
}

std::string readMode(const std::string& path) {
  std::string mode;
  if (FILE* file = std::fopen(path.c_str(), "rb")) {
    char buffer[64];
    const std::size_t count = std::fread(buffer, 1, sizeof buffer, file);
    mode.assign(buffer, count);
    std::fclose(file);
  }
  while (!mode.empty() && (mode.back() == '\n' || mode.back() == ' ')) mode.pop_back();
  return mode.empty() ? "ok" : mode;
}

}

int main(int argc, char** argv) {
  ::prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0);
  const std::string directory = directoryOf(argv[0]);
  const std::string mode = readMode(directory + "/helper-mode");
  if (mode == "preflight") return 3;
  std::string problem;
  int type = 0;
  socklen_t length = sizeof type;
  if (argc != 3 || std::strcmp(argv[1], "--socket-fd") || std::strcmp(argv[2], "104")) problem = "arguments";
  else if (environ && environ[0]) problem = std::string("environment ") + environ[0];
  else if (::getsockopt(104, SOL_SOCKET, SO_TYPE, &type, &length) < 0 || type != SOCK_SEQPACKET) problem = "fd 104";
  const int lock = ::open((directory + "/helper.lock").c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
  if (problem.empty() && (lock < 0 || ::flock(lock, LOCK_EX | LOCK_NB) < 0)) problem = "another helper holds the device";
  const std::string hello = problem.empty() ? "helper ok" : "helper invalid: " + problem;
  ::send(104, hello.data(), hello.size(), MSG_NOSIGNAL);
  if (!problem.empty()) return 3;
  char buffer[64];
  for (;;) {
    pollfd item{104, POLLIN, 0};
    ::poll(&item, 1, 1000);
    const ssize_t count = ::recv(104, buffer, sizeof buffer, MSG_DONTWAIT);
    if (count == 0 || (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) break;
  }
  if (mode == "slow") ::usleep(500000);
  if (mode == "linger") {
    ::signal(SIGTERM, SIG_IGN);
    for (;;) ::pause();
  }
  return 0;
}
