#include "platform/posix/Files.h"
#include "platform/posix/Text.h"
#include "platform/posix/Time.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace {
unsigned fileSyncs = 0, directorySyncs = 0;
bool failFileSync = false, failDirectorySync = false;
void check(bool okay, const char* why) {
  if (!okay) { std::fprintf(stderr, "posix files: %s (errno %d)\n", why, errno); std::exit(1); }
}
}

extern "C" int __real_fsync(int);
extern "C" int __wrap_fsync(int fd) {
  struct stat info{};
  if (::fstat(fd, &info) != 0) return -1;
  const bool directory = S_ISDIR(info.st_mode);
  if (directory) ++directorySyncs;
  else ++fileSyncs;
  if (directory ? failDirectorySync : failFileSync) { errno = EIO; return -1; }
  return __real_fsync(fd);
}

int main() {
  using namespace awtrix::posix;
  int number = 7;
  check(parseInteger("65535", 1, 65535, number) && number == 65535, "integer upper bound");
  for (const auto* invalid : {"", "65536", "0", "1x", "9999999999999999999999"})
    check(!parseInteger(invalid, 1, 65535, number) && number == 65535,
          "invalid integer leaves output unchanged");
  check(!parseInteger(nullptr, 1, 65535, number), "null integer input");
  check(wallClock(0) == "1970-01-01T00:00:00Z" && wallClock(951827696) == "2000-02-29T12:34:56Z",
        "UTC wall clock includes leap day");
  check(printable(std::string_view("a\0\n\x7f\xffz", 6), 5) == "a????",
        "diagnostic text is bounded and printable ASCII");
  char pattern[] = "/tmp/awtrix-posix-XXXXXX";
  const char* made = ::mkdtemp(pattern);
  check(made, "temporary directory");
  const std::string root(made), path = root + "/value";
  const mode_t oldMask = ::umask(0777);
  check(replaceText(path, "old"), "atomic write with restrictive umask");
  ::umask(oldMask);
  struct stat info{};
  check(::stat(path.c_str(), &info) == 0 && (info.st_mode & 0777) == 0600, "private file mode");
  check(fileSyncs == 1 && directorySyncs == 1, "file and rename are both durable");
  failFileSync = true;
  check(!replaceText(path, "new") && errno == EIO, "file sync failure is reported");
  failFileSync = false;
  std::string text;
  check(readText(path, text) && text == "old", "failed file sync preserves old file");
  failDirectorySync = true;
  check(!replaceText(path, "new") && errno == EIO, "directory sync failure is reported");
  failDirectorySync = false;
  check(readText(path, text) && text == "new", "rename precedes directory sync");
  check(std::distance(std::filesystem::directory_iterator(root), std::filesystem::directory_iterator{}) == 1,
        "temporary files are removed after failure");

  auto directory = openPrivateDirectory(root, false);
  check(directory.valid(), "open private directory");
  text = "unchanged";
  check(!readRegularFile(directory.get(), "value", 2, text) && text == "unchanged", "bounded regular read");
  check(readRegularFile(directory.get(), "value", 3, text) && text == "new", "relative regular read");
  check(::symlink("value", (root + "/link").c_str()) == 0, "file symlink fixture");
  check(!readRegularFile(directory.get(), "link", 100, text), "refuse file symlink");
  check(::mkfifo((root + "/pipe").c_str(), 0600) == 0, "FIFO fixture");
  check(!readRegularFile(directory.get(), "pipe", 100, text), "refuse FIFO without blocking");
  check(!readRegularFile(directory.get(), ".", 100, text), "refuse directory as regular file");

  const std::string leaf = root + "/private";
  check(openPrivateDirectory(leaf, true).valid(), "create private directory");
  check(::chmod(leaf.c_str(), 0755) == 0, "unsafe directory fixture");
  check(!openPrivateDirectoryAt(directory.get(), "private", true).valid() && errno == EPERM,
        "strict creation refuses unsafe existing directory");
  check(!openPrivateDirectory(leaf, false).valid(), "reader never repairs mode");
  check(openPrivateDirectory(leaf, true).valid(), "writer can tighten owned directory");
  check(::stat(leaf.c_str(), &info) == 0 && (info.st_mode & 07777) == 0700, "repaired mode");
  check(::symlink("private", (root + "/dir-link").c_str()) == 0, "directory symlink fixture");
  check(!openPrivateDirectory(root + "/dir-link", true).valid(), "refuse directory symlink");
  std::filesystem::remove_all(root);
}
