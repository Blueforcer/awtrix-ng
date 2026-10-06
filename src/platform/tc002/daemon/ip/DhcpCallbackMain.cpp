#include "platform/posix/Files.h"
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <sys/stat.h>
#include <unistd.h>

#include "platform/tc002/daemon/ip/DhcpRecordFormat.h"

using namespace awtrix::tc002d::ip;

static_assert(kMaxDhcpRecord <= PIPE_BUF, "one write per record keeps records whole");

int main(int argc, char** argv) {
  const int64_t capturedMs = awtrix::posix::monotonicMs();
  if (argc != 2) {
    std::fputs("usage: dhcp-callback EVENT (run by udhcpc -s)\n", stderr);
    return 2;
  }
  char record[kMaxDhcpRecord];
  const std::size_t size = formatDhcpRecord(
      argv[1], capturedMs, [](const char* key) { return std::getenv(key); }, record, sizeof(record));
  if (!size) {
    std::fputs("dhcp callback: event not recordable\n", stderr);
    return 1;
  }
  struct stat pipeInfo {};
  if (fstat(kDhcpEventFd, &pipeInfo) != 0 || !S_ISFIFO(pipeInfo.st_mode)) {
    std::fputs("dhcp callback: event pipe missing\n", stderr);
    return 1;
  }
  for (int attempt = 0; attempt < 8; ++attempt) {
    const ssize_t written = write(kDhcpEventFd, record, size);
    if (written == static_cast<ssize_t>(size)) return 0;
    if (written < 0 && errno == EINTR) continue;
    break;
  }
  std::fputs("dhcp callback: event pipe write failed\n", stderr);
  return 1;
}
