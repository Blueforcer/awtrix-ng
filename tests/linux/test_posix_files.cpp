#include "../support.h"
#include "platform/posix/Files.h"
#include "platform/posix/Text.h"
#include "platform/posix/Time.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
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
  check(MappedFile(path, 3).valid() && MappedFile(path, 3).bytes() == "new", "a mapped file shows its bytes");
  check(!MappedFile(path, 2).valid(), "bounded mapping");
  check(!MappedFile(root + "/link", 100).valid(), "refuse mapping a file symlink");
  check(!MappedFile(root + "/pipe", 100).valid(), "refuse mapping a FIFO without blocking");
  check(!MappedFile(root, 100).valid(), "refuse mapping a directory");
  {
    const UniqueFd held(::open((root + "/mapped-by-fd").c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600));
    check(writeAll(held.get(), "abc", 3) && MappedFile(held.get(), 3).bytes() == "abc", "a descriptor maps its file");
    check(!MappedFile(held.get(), 2).valid(), "bounded by descriptor too");
    check(!MappedFile(-1, 3).valid(), "a bad descriptor maps nothing");
  }
  check(replaceText(root + "/empty", "") && MappedFile(root + "/empty", 0).valid() &&
            MappedFile(root + "/empty", 0).bytes().empty(),
        "an empty file maps to no bytes");
  {
    const UniqueFd file(::open((root + "/placed").c_str(), O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600));
    char back[6] = {};
    check(file.valid() && pwriteAll(file.get(), 2, "cdef", 4) && pwriteAll(file.get(), 0, "ab", 2) &&
              preadAll(file.get(), 0, back, 6) && std::string_view(back, 6) == "abcdef",
          "positioned writes land at their offsets");
    check(!pwriteAll(-1, 0, "x", 1) && errno == EBADF, "a positioned write to no file fails");
  }
  {
    check(replaceText(root + "/mapped", "kept"), "mapped fixture");
    const MappedFile mapped(root + "/mapped", 4);
    check(::unlink((root + "/mapped").c_str()) == 0 && mapped.bytes() == "kept",
          "a mapping keeps the bytes of a file deleted meanwhile");
  }

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

  const pid_t gone = awtrix::test::endedProcess();
  const std::string dead = root + "/old-" + std::to_string(gone);
  const std::string stale = dead + "-a1B2c3", shared = dead + "-d4E5f6", deep = dead + "-g7H8i9";
  const std::string alive = root + "/old-1-j0K1l2", other = root + "/older-" + std::to_string(gone) + "-m3N4o5";
  for (const std::string& fixture : {stale, shared, deep, alive, other})
    check(openPrivateDirectory(fixture, true).valid() && replaceText(fixture + "/a", "x"), "directory fixture");
  check(replaceText(stale + "/b", "y") && ::chmod(shared.c_str(), 0755) == 0 &&
            openPrivateDirectory(deep + "/inner", true).valid() &&
            ::symlink("older", (dead + "-p6Q7r8").c_str()) == 0 && replaceText(dead + "-s9T0u1x", "x") &&
            ::mkdir((dead + "-v2W3").c_str(), 0700) == 0,
        "stale, shared, nested, linked and misnamed fixtures");
  std::string created;
  {
    const PrivateTemporaryDirectory first(root, "old"), second(root, "old");
    check(!std::filesystem::exists(stale), "a stale private directory of an ended process goes with its files");
    check(!std::filesystem::exists(alive), "also when its process id has come to name another process");
    check(std::filesystem::exists(shared + "/a") && std::filesystem::exists(deep + "/inner") &&
              std::filesystem::exists(other + "/a") &&
              std::filesystem::is_symlink(dead + "-p6Q7r8") && std::filesystem::exists(dead + "-s9T0u1x") &&
              std::filesystem::exists(dead + "-v2W3"),
          "shared or nested directories, other prefixes, links and other names stay");
    created = first.path();
    const std::string own = root + "/old-" + std::to_string(::getpid()) + "-";
    check(first.valid() && second.valid() && created.rfind(own, 0) == 0 && created.size() == own.size() + 6 &&
              second.path() != created && std::filesystem::exists(created),
          "a new directory names its creator and one whose creator still runs stays");
    check(::stat(created.c_str(), &info) == 0 && (info.st_mode & 07777) == 0700, "the new directory is private");
    check(replaceText(created + "/one", "1") && replaceText(created + "/two", "2"), "files in it");
  }
  check(!std::filesystem::exists(created), "it goes with its files");
  std::filesystem::remove_all(root);
}
