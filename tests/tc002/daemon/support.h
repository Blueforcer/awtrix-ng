#pragma once

#include "../../support.h"

#include <fcntl.h>
#include <ftw.h>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>

#include "platform/posix/Files.h"
#include "platform/tc002/daemon/EventLoop.h"
#include "platform/tc002/daemon/Log.h"

namespace posix = awtrix::posix;

namespace tc002d_test {

using awtrix::test::failures;
using awtrix::test::check;
using awtrix::test::finish;

class TempDir {
 public:
  TempDir() {
    char pattern[] = "/tmp/tc002d-test-XXXXXX";
    const char* made = ::mkdtemp(pattern);
    path_ = made ? made : "";
  }
  ~TempDir() {
    if (path_.empty()) return;
    ::nftw(path_.c_str(), [](const char* path, const struct stat*, int, struct FTW*) { return ::remove(path); }, 16,
           FTW_DEPTH | FTW_PHYS);
  }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;
  const std::string& path() const { return path_; }
  std::string operator/(const std::string& name) const { return path_ + "/" + name; }

 private:
  std::string path_;
};

inline bool writeFile(const std::string& path, const std::string& content) {
  posix::makeDirectories(posix::parentDirectory(path), 0755);
  FILE* file = std::fopen(path.c_str(), "wb");
  if (!file) return false;
  const bool okay = std::fwrite(content.data(), 1, content.size(), file) == content.size();
  return std::fclose(file) == 0 && okay;
}

inline std::string readFile(const std::string& path) {
  std::string out;
  FILE* file = std::fopen(path.c_str(), "rb");
  if (!file) return out;
  char buffer[4096];
  std::size_t count;
  while ((count = std::fread(buffer, 1, sizeof buffer, file)) > 0) out.append(buffer, count);
  std::fclose(file);
  return out;
}

inline bool exists(const std::string& path) {
  struct stat info{};
  return ::lstat(path.c_str(), &info) == 0;
}

inline bool makePipe(int ends[2], int flags, const std::string& what) {
  ends[0] = ends[1] = -1;
  if (::pipe2(ends, flags) == 0) return true;
  check(false, what + " pipe (" + std::strerror(errno) + ")");
  return false;
}

inline bool makeLink(const std::string& target, const std::string& path) {
  if (::symlink(target.c_str(), path.c_str()) == 0) return true;
  check(false, "link " + path + " (" + std::strerror(errno) + ")");
  return false;
}

inline bool runUntil(awtrix::tc002d::EventLoop& loop, const std::function<bool()>& done, int timeoutMs) {
  const int64_t deadline = posix::monotonicMs() + timeoutMs;
  while (!done()) {
    if (posix::monotonicMs() >= deadline) return false;
    loop.runOnce(10);
  }
  return true;
}

inline void quietLogs() { awtrix::tc002d::Log::mirrorToStderr(std::getenv("TC002D_TEST_VERBOSE") != nullptr); }

}
