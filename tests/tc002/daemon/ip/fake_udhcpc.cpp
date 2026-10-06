#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <vector>

// Stands in for BusyBox udhcpc: appends its command line to FAKE_UDHCPC_LOG (default
// /tmp/fake-udhcpc.argv), prints progress and an error to stderr as udhcpc does, runs the -s
// program for deconfig and bound, runs it for renew on SIGUSR1 and exits on SIGTERM.
namespace {

using Environment = std::vector<std::pair<std::string, std::string>>;

void runScript(const std::string& script, const char* event, const Environment& environment) {
  const pid_t child = fork();
  if (child == 0) {
    for (const auto& entry : environment) setenv(entry.first.c_str(), entry.second.c_str(), 1);
    execl(script.c_str(), script.c_str(), event, static_cast<char*>(nullptr));
    _exit(127);
  }
  int status = 0;
  if (child > 0) waitpid(child, &status, 0);
}

}

int main(int argc, char** argv) {
  std::string script, interfaceName = "wlan0", address = "10.77.0.23";
  for (int i = 1; i + 1 < argc; ++i) {
    if (!std::strcmp(argv[i], "-s")) script = argv[++i];
    else if (!std::strcmp(argv[i], "-i")) interfaceName = argv[++i];
    else if (!std::strcmp(argv[i], "-r")) address = argv[++i];
  }
  const char* logPath = std::getenv("FAKE_UDHCPC_LOG");
  if (FILE* log = std::fopen(logPath ? logPath : "/tmp/fake-udhcpc.argv", "a")) {
    for (int i = 0; i < argc; ++i) std::fprintf(log, "%s%s", i ? " " : "", argv[i]);
    std::fputc('\n', log);
    std::fclose(log);
  }
  if (script.empty()) return 2;
  const char chatter[] = "udhcpc: started, v1.37.0\nudhcpc: broadcasting discover\nudhcpc: fake client failure\n";
  const char stdoutChatter[] = "stdout chatter\n";
  if (write(STDERR_FILENO, chatter, sizeof(chatter) - 1) < 0 ||
      write(STDOUT_FILENO, stdoutChatter, sizeof(stdoutChatter) - 1) < 0)
    return 3;
  sigset_t wanted;
  sigemptyset(&wanted);
  sigaddset(&wanted, SIGUSR1);
  sigaddset(&wanted, SIGTERM);
  sigprocmask(SIG_BLOCK, &wanted, nullptr);
  const Environment lease = {{"interface", interfaceName},       {"ip", address},
                             {"subnet", "255.255.255.0"},        {"router", "10.77.0.1"},
                             {"dns", "10.77.0.1 10.77.0.2"},     {"lease", "3600"},
                             {"ntpsrv", "10.77.0.1"}};
  runScript(script, "deconfig", {{"interface", interfaceName}});
  runScript(script, "bound", lease);
  for (;;) {
    int received = 0;
    if (sigwait(&wanted, &received) != 0) return 1;
    if (received == SIGTERM) return 0;
    runScript(script, "renew", lease);
  }
}
