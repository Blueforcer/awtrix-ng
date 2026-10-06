// Stand-in for awtrix-linux used by test_tc002d_runtime. It checks the supervisor's launch
// contract (arguments, the descriptors of RuntimeContract.h, environment, session), records the
// result in <data>/report-<pid>, the boot intro it was given in <data>/intro-<pid>, the CA and
// web update flags in <data>/update-args-<pid>, its start reason as a line of <data>/starts and
// its --uid ("none" without one) as a line of <data>/uids, then behaves according to <data>/mode:
//   ok        hello, ready, log every received datagram, send lines from <data>/outbox
//   crash     exit 1 right away
//   closed    close the supervisor channel, then exit 1 (a start that fails before readiness)
//   silent    never report readiness; exit 0 on SIGTERM
//   stubborn  never report readiness and ignore SIGTERM
//   badready  hello, then a readiness report for a 32x8 panel without input
//   abrupt    like ok, but exit as soon as the outbox has been sent
// Any process of the same user may ptrace it, so a test can hold it unreapable.
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern char** environ;

namespace {

volatile sig_atomic_t terminated = 0;
void onTerm(int) { terminated = 1; }

std::string readAll(const std::string& path) {
  std::string out;
  if (FILE* file = std::fopen(path.c_str(), "rb")) {
    char buffer[4096];
    std::size_t count;
    while ((count = std::fread(buffer, 1, sizeof buffer, file)) > 0) out.append(buffer, count);
    std::fclose(file);
  }
  return out;
}

void writeAll(const std::string& path, const std::string& text, bool append) {
  if (FILE* file = std::fopen(path.c_str(), append ? "ab" : "wb")) {
    std::fwrite(text.data(), 1, text.size(), file);
    std::fclose(file);
  }
}

std::string contract(int argc, char** argv, const std::string& data, bool& audio, std::string& voice,
                     std::string& intro, std::string& update, std::string& start, std::string& uid) {
  const char* expected[] = {nullptr, "--board", "tc002", "--tc002-input-fds", "100,101", "--supervisor-fd", "103",
                            "--data", nullptr, "--webui", nullptr, "--lan", "--port", nullptr,
                            "--start-reason", nullptr};
  constexpr int kBase = sizeof expected / sizeof expected[0];
  intro = "none";
  uid = "none";
  voice = "none";
  if (argc < kBase) return "argument count " + std::to_string(argc);
  for (int i = 1; i < kBase; ++i)
    if (expected[i] && std::strcmp(argv[i], expected[i]) != 0) return std::string("argument ") + argv[i];
  start = argv[kBase - 1];
  if (start != "poweron" && start != "software" && start != "panic" && start != "watchdog")
    return "start reason " + start;
  int next = kBase;
  if (next + 1 < argc && std::strcmp(argv[next], "--uid") == 0) {
    uid = argv[next + 1];
    if (uid.size() != 12 || uid.find_first_not_of("0123456789abcdef") != std::string::npos) return "uid " + uid;
    next += 2;
  }
  audio = next + 1 < argc && std::strcmp(argv[next], "--tc002-audio-fd") == 0;
  if (audio) {
    if (std::strcmp(argv[next + 1], "104") != 0) return std::string("argument ") + argv[next + 1];
    next += 2;
    if (next + 1 < argc && std::strcmp(argv[next], "--speech-voice") == 0) {
      voice = argv[next + 1];
      next += 2;
    }
  }
  if (next < argc && std::strcmp(argv[next], "--boot-intro") == 0) {
    intro = "intro";
    ++next;
    if (next + 1 < argc && std::strcmp(argv[next], "--boot-sound") == 0) {
      intro += std::string(" ") + argv[next + 1];
      next += 2;
    }
  }
  for (const char* flag : {"--ca-file", "--update-state", "--release-root", "--update-dir"}) {
    if (next + 1 < argc && std::strcmp(argv[next], flag) == 0) {
      update += std::string(update.empty() ? "" : " ") + flag + " " + argv[next + 1];
      next += 2;
    }
  }
  if (next != argc) return std::string("unexpected argument ") + argv[next];
  struct stat info{};
  if (::stat(argv[10], &info) < 0 || !S_ISREG(info.st_mode)) return "web UI file";
  for (int fd : {0, 1, 2, 100, 101})
    if (::fcntl(fd, F_GETFD) < 0) return "descriptor " + std::to_string(fd) + " missing";
  int type = 0;
  socklen_t length = sizeof type;
  if (::getsockopt(103, SOL_SOCKET, SO_TYPE, &type, &length) < 0 || type != SOCK_SEQPACKET ||
      !(::fcntl(103, F_GETFL) & O_NONBLOCK))
    return "supervisor descriptor 103";
  if (DIR* directory = opendir("/proc/self/fd")) {
    const int own = dirfd(directory);
    std::string extra;
    while (const dirent* entry = readdir(directory)) {
      if (entry->d_name[0] == '.') continue;
      const int fd = std::atoi(entry->d_name);
      const bool inherited = fd <= 2 || fd == 100 || fd == 101 || fd == 103 || (audio && fd == 104);
      if (fd != own && !inherited) extra += " " + std::to_string(fd);
    }
    closedir(directory);
    if (!extra.empty()) return "leaked descriptors" + extra;
  }
  std::vector<std::string> environment;
  for (char** entry = environ; *entry; ++entry) environment.push_back(*entry);
  bool path = false, home = false;
  for (const auto& entry : environment) {
    if (entry == "PATH=/bin:/sbin:/usr/bin:/usr/sbin") path = true;
    else if (entry == "HOME=" + data) home = true;
    else if (entry.compare(0, 3, "TZ=") != 0) return "unexpected environment " + entry;
  }
  if (!path || !home) return "environment incomplete";
  if (::getsid(0) != ::getpid()) return "not a session leader";
  return "";
}

void sendDatagram(const std::string& datagram) { ::send(103, datagram.data(), datagram.size(), MSG_NOSIGNAL); }

void sayHello() { sendDatagram("{\"v\":2,\"type\":\"hello\",\"version\":\"fake-1\"}"); }

void reportReady(bool valid) {
  sendDatagram(valid ? "{\"v\":2,\"type\":\"ready\",\"board\":\"tc002\",\"width\":52,\"height\":16,\"input\":true}"
                     : "{\"v\":2,\"type\":\"ready\",\"board\":\"tc002\",\"width\":32,\"height\":8,\"input\":false}");
}

void waitForTerm() {
  while (!terminated) ::pause();
}

}

int main(int argc, char** argv) {
  ::prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0);
  const std::string data = argc > 8 ? argv[8] : "";
  bool audio = false;
  std::string voice;
  std::string intro;
  std::string update;
  std::string start;
  std::string uid;
  const std::string problem = contract(argc, argv, data, audio, voice, intro, update, start, uid);
  writeAll(data + "/starts", start + "\n", true);
  writeAll(data + "/uids", uid + "\n", true);
  writeAll(data + "/intro-" + std::to_string(::getpid()), intro + "\n", false);
  writeAll(data + "/voice-" + std::to_string(::getpid()), voice + "\n", false);
  writeAll(data + "/update-args-" + std::to_string(::getpid()), update + "\n", false);
  if (audio) {
    std::string heard = "no helper message";
    pollfd item{104, POLLIN, 0};
    char buffer[256];
    if (::poll(&item, 1, 2000) > 0) {
      const ssize_t count = ::recv(104, buffer, sizeof buffer, 0);
      heard = count > 0 ? std::string(buffer, static_cast<std::size_t>(count)) : "helper closed";
    }
    writeAll(data + "/audio-" + std::to_string(::getpid()), heard + "\n", false);
  }
  std::string mode = readAll(data + "/mode");
  while (!mode.empty() && (mode.back() == '\n' || mode.back() == ' ')) mode.pop_back();
  if (mode.empty()) mode = "ok";
  writeAll(data + "/report-" + std::to_string(::getpid()), problem.empty() ? "valid\n" : "invalid: " + problem + "\n",
           false);
  std::printf("fake runtime pid %d mode %s\n", static_cast<int>(::getpid()), mode.c_str());
  std::fflush(stdout);
  struct sigaction action{};
  action.sa_handler = onTerm;
  sigemptyset(&action.sa_mask);
  sigaction(SIGTERM, &action, nullptr);

  if (mode == "crash") return 1;
  if (mode == "closed") {
    ::close(103);
    ::usleep(200000);
    return 1;
  }
  if (mode == "stubborn") {
    ::signal(SIGTERM, SIG_IGN);
    for (;;) ::pause();
  }
  if (mode == "silent") {
    waitForTerm();
    return 0;
  }
  sayHello();
  if (mode == "badready") {
    reportReady(false);
    waitForTerm();
    return 0;
  }
  reportReady(true);
  while (!terminated) {
    pollfd item{103, POLLIN, 0};
    ::poll(&item, 1, 20);
    char buffer[4096];
    ssize_t count;
    while ((count = ::recv(103, buffer, sizeof buffer, MSG_DONTWAIT)) > 0)
      writeAll(data + "/received.log", std::string(buffer, static_cast<std::size_t>(count)) + "\n", true);
    if (count == 0) break;
    const std::string outbox = data + "/outbox";
    const std::string lines = readAll(outbox);
    if (lines.empty()) continue;
    ::unlink(outbox.c_str());
    std::size_t start = 0;
    bool rebooting = false;
    while (start < lines.size()) {
      auto end = lines.find('\n', start);
      if (end == std::string::npos) end = lines.size();
      const std::string line = lines.substr(start, end - start);
      if (!line.empty()) ::send(103, line.data(), line.size(), MSG_NOSIGNAL);
      if (line.find("\"type\":\"reboot\"") != std::string::npos) rebooting = true;
      start = end + 1;
    }
    if (rebooting && mode != "abrupt") ::usleep(200000);
    if (rebooting || mode == "abrupt") {
      writeAll(data + "/exit-" + std::to_string(::getpid()), terminated ? "term\n" : "self\n", false);
      return 0;
    }
  }
  writeAll(data + "/exit-" + std::to_string(::getpid()), terminated ? "term\n" : "closed\n", false);
  return 0;
}
