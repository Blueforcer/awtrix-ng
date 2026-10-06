// ControlSocket framing, limits and permissions; the ctl client of the real daemon binary
// (argv[1]) talks to a socket served by this process.
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>

#include <atomic>
#include <cerrno>
#include <cstring>
#include <thread>
#include <vector>

#include "platform/tc002/daemon/ControlSocket.h"
#include "support.h"

using namespace awtrix::tc002d;
using tc002d_test::check;
using tc002d_test::makePipe;
using tc002d_test::runUntil;
using tc002d_test::TempDir;

namespace {

class ChildWatcher : public Service {
 public:
  const char* name() const override { return "watcher"; }
  bool start(int64_t) override { return true; }
  bool onChildExit(pid_t pid, int status, int64_t) override {
    if (pid != pid_) return false;
    status_ = status;
    done_ = true;
    return true;
  }
  void watch(pid_t pid) {
    pid_ = pid;
    done_ = false;
  }
  bool done() const { return done_; }
  int status() const { return status_; }

 private:
  pid_t pid_ = -1;
  int status_ = 0;
  bool done_ = false;
};

int connectTo(const std::string& path) {
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  std::strncpy(address.sun_path, path.c_str(), sizeof address.sun_path - 1);
  const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (::connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof address) < 0) {
    ::close(fd);
    return -1;
  }
  return fd;
}

// Runs a blocking client on a thread while the loop serves it.
std::string ask(EventLoop& loop, const std::string& path, const std::string& request, bool* ok = nullptr) {
  std::atomic<bool> done{false};
  std::string reply, error;
  bool success = false;
  std::thread client([&] {
    success = controlRequest(path, request, reply, 3000, error);
    done = true;
  });
  runUntil(loop, [&] { return done.load(); }, 4000);
  client.join();
  if (ok) *ok = success;
  return success ? reply : "ERROR " + error;
}

std::string readReply(int fd, int timeoutMs) {
  std::string input;
  pollfd item{fd, POLLIN, 0};
  while (::poll(&item, 1, timeoutMs) > 0) {
    char buffer[512];
    const ssize_t count = ::recv(fd, buffer, sizeof buffer, 0);
    if (count <= 0) break;
    input.append(buffer, static_cast<std::size_t>(count));
  }
  return input.size() >= 4 ? input.substr(4) : input;
}

struct Result {
  int status = -1;
  std::string output;
};

Result runCtl(EventLoop& loop, ChildWatcher& watcher, const std::string& binary, std::vector<std::string> args,
              const std::string& input = "") {
  int out[2], in[2];
  if (!makePipe(out, O_CLOEXEC, "ctl output")) return {};
  if (!makePipe(in, O_CLOEXEC, "ctl input")) {
    ::close(out[0]);
    ::close(out[1]);
    return {};
  }
  const pid_t pid = ::fork();
  if (pid == 0) {
    ::dup2(out[1], STDOUT_FILENO);
    ::dup2(out[1], STDERR_FILENO);
    ::dup2(in[0], STDIN_FILENO);
    std::vector<char*> argv{const_cast<char*>(binary.c_str()), const_cast<char*>("ctl")};
    for (auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);
    ::execv(binary.c_str(), argv.data());
    _exit(127);
  }
  watcher.watch(pid);
  ::close(out[1]);
  ::close(in[0]);
  (void)!::write(in[1], input.data(), input.size());
  ::close(in[1]);
  ::fcntl(out[0], F_SETFL, O_NONBLOCK);
  Result result;
  runUntil(loop, [&] {
    char buffer[512];
    ssize_t count;
    while ((count = ::read(out[0], buffer, sizeof buffer)) > 0) result.output.append(buffer, static_cast<std::size_t>(count));
    return watcher.done();
  }, 5000);
  char buffer[512];
  ssize_t count;
  while ((count = ::read(out[0], buffer, sizeof buffer)) > 0) result.output.append(buffer, static_cast<std::size_t>(count));
  ::close(out[0]);
  result.status = watcher.done() && WIFEXITED(watcher.status()) ? WEXITSTATUS(watcher.status()) : -1;
  return result;
}

void framing(const std::string& binary) {
  TempDir dir;
  ControlOptions options;
  options.path = dir / "control.sock";
  options.peerUid = ::geteuid();
  options.clientTimeoutMs = 300;
  options.maxClients = 2;
  ControlSocket control(options);
  control.add("echo", [](std::string_view payload) { return "echo:" + std::string(payload); });
  control.add("status", [](std::string_view) { return std::string("{\"ok\":true}"); });
  control.add("throws", [](std::string_view) -> std::string { throw 1; });
  ChildWatcher watcher;
  EventLoop loop;
  loop.add(watcher);
  loop.add(control);
  check(loop.start() && control.listening(), "control socket listens");
  struct stat info{};
  check(::stat(options.path.c_str(), &info) == 0 && S_ISSOCK(info.st_mode) && (info.st_mode & 0777) == 0600,
        "socket is mode 0600");

  check(ask(loop, options.path, "echo\nhello\nworld") == "echo:hello\nworld", "payload keeps its newlines");
  check(ask(loop, options.path, "echo") == "echo:", "command without payload");
  check(ask(loop, options.path, "commands") == "[\"commands\",\"echo\",\"status\",\"throws\"]", "commands listed");
  check(ask(loop, options.path, "nope") == "{\"ok\":false,\"error\":\"unknown command: nope\"}", "unknown command");
  check(ask(loop, options.path, "throws") == "{\"ok\":false,\"error\":\"internal error\"}", "handler failure contained");
  bool ok = true;
  check(ask(loop, options.path, std::string(5000, 'x'), &ok).find("too large") != std::string::npos && !ok,
        "client refuses oversized requests");

  const int big = connectTo(options.path);
  const std::string header("\x00\x00\x13\x88", 4);
  (void)!::send(big, header.data(), header.size(), MSG_NOSIGNAL);
  runUntil(loop, [] { return false; }, 100);
  check(readReply(big, 200) == "{\"ok\":false,\"error\":\"request too large\"}", "server refuses a 5000-byte frame");
  ::close(big);

  const int slow = connectTo(options.path);
  (void)!::send(slow, "\x00\x00", 2, MSG_NOSIGNAL);
  runUntil(loop, [] { return false; }, 500);
  check(control.clients() == 0, "silent partial client dropped at its deadline");
  char byte;
  check(::recv(slow, &byte, 1, MSG_DONTWAIT) == 0, "server closed the silent connection");
  ::close(slow);

  std::vector<int> many;
  for (int i = 0; i < 5; ++i) many.push_back(connectTo(options.path));
  for (int fd : many) {
    const std::string request = encodeControlFrame("echo\nx");
    (void)!::send(fd, request.data(), request.size(), MSG_NOSIGNAL);
  }
  unsigned answered = 0;
  runUntil(loop, [&] {
    for (int& fd : many) {
      if (fd < 0) continue;
      char buffer[64];
      const ssize_t count = ::recv(fd, buffer, sizeof buffer, MSG_DONTWAIT);
      if (count > 4 && std::string(buffer + 4, static_cast<std::size_t>(count - 4)) == "echo:x") {
        ++answered;
        ::close(fd);
        fd = -1;
      }
    }
    check(control.clients() <= 2, "at most maxClients connections in service");
    return answered == many.size();
  }, 2000);
  check(answered == 5, "queued connections are served once slots free up");

  Result result = runCtl(loop, watcher, binary, {"--run-dir", dir.path(), "echo", "from binary"});
  check(result.status == 0 && result.output == "echo:from binary\n", "ctl binary prints the reply");
  result = runCtl(loop, watcher, binary, {"--run-dir", dir.path(), "echo", "-"}, "stdin payload");
  check(result.status == 0 && result.output == "echo:stdin payload\n", "ctl reads the payload from stdin");
  result = runCtl(loop, watcher, binary, {"--run-dir", dir.path() + "/missing", "status"});
  check(result.status == 1 && result.output.find("connect") != std::string::npos, "ctl reports a missing daemon");
  result = runCtl(loop, watcher, binary, {});
  check(result.status == 2, "ctl without a command is a usage error");

  loop.requestStop("test");
  runUntil(loop, [&] { return loop.finished(); }, 2000);
  check(control.stopped() && !tc002d_test::exists(options.path), "socket removed on stop");
}

void peerCredentials() {
  TempDir dir;
  ControlOptions options;
  options.path = dir / "control.sock";
  options.peerUid = ::geteuid() + 1;
  ControlSocket control(options);
  control.add("status", [](std::string_view) { return std::string("secret"); });
  EventLoop loop;
  loop.add(control);
  loop.start();
  check(ask(loop, options.path, "status") == "{\"ok\":false,\"error\":\"permission denied\"}",
        "foreign uid refused before any handler runs");
}

void stalePaths() {
  TempDir dir;
  const std::string path = dir / "control.sock";
  {
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, path.c_str(), sizeof address.sun_path - 1);
    posix::UniqueFd stale(::socket(AF_UNIX, SOCK_STREAM, 0));
    ::bind(stale.get(), reinterpret_cast<const sockaddr*>(&address), sizeof address);
  }
  ControlOptions options;
  options.path = path;
  options.peerUid = ::geteuid();
  ControlSocket control(options);
  control.start(0);
  check(control.listening(), "stale socket from a previous run replaced");
  control.requestStop(0);

  tc002d_test::writeFile(path, "not a socket");
  ControlSocket blocked(options);
  blocked.start(0);
  check(!blocked.listening() && tc002d_test::readFile(path) == "not a socket", "regular file never deleted");
  check(blocked.nextDeadlineMs() == options.bindRetryMs, "bind retried later");
}

}

int main(int argc, char** argv) {
  tc002d_test::quietLogs();
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s AWTRIX_TC002D_BINARY\n", argv[0]);
    return 2;
  }
  framing(argv[1]);
  peerCredentials();
  stalePaths();
  return tc002d_test::finish("tc002d control");
}
