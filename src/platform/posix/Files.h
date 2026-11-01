#pragma once

#include <sys/types.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "platform/posix/UniqueFd.h"
#include "platform/posix/Time.h"

// File and descriptor helpers of the Linux programs: the TC002 daemon, its tools and the runtime.
namespace awtrix {
namespace posix {


// Exactly size bytes, retrying after EINTR; false on an error or a short file (errno EIO).
bool readAll(int fd, void* data, std::size_t size);
bool preadAll(int fd, uint64_t offset, void* data, std::size_t size);
bool writeAll(int fd, const void* data, std::size_t size);
bool pwriteAll(int fd, uint64_t offset, const void* data, std::size_t size);

bool readText(const std::string& path, std::string& out, std::size_t maximum = 4096);
// No symlinks or special files; exactly the bounded fstat size must be readable.
bool readRegularFile(int directory, const std::string& path, std::size_t maximum, std::string& out);
bool writeText(const std::string& path, std::string_view text);
// A private (0600) file written beside path, synced and renamed over it: a reader finds the old
// text or the new one, never part of either.
bool replaceText(const std::string& path, std::string_view text);
std::string trimmed(std::string_view text);
bool numeric(const char* text);
// Lowercase hex of size bytes.
std::string hexBytes(const void* data, std::size_t size);

// Creates the directory (0700) when missing and refuses symlinks, foreign owners and
// anything that is not a directory; the mode is tightened to 0700.
bool ensurePrivateDirectory(const std::string& path);
// Opens an owned directory without following the final symlink. Creation never makes parents.
// repair tightens existing permissions; readers and security-sensitive callers can refuse them.
UniqueFd openPrivateDirectoryAt(int directory, const std::string& path, bool create, bool repair = false);
UniqueFd openPrivateDirectory(const std::string& path, bool create);
bool makeDirectories(const std::string& path, unsigned mode);
bool fsyncDirectory(const std::string& path);
std::string parentDirectory(const std::string& path);

class MappedFile {
 public:
  MappedFile(const std::string& path, std::size_t maximum);
  ~MappedFile();
  MappedFile(const MappedFile&) = delete;
  MappedFile& operator=(const MappedFile&) = delete;

  bool valid() const { return valid_; }
  std::string_view bytes() const { return {static_cast<const char*>(data_), size_}; }

 private:
  void* data_ = nullptr;
  std::size_t size_ = 0;
  bool valid_ = false;
};

class PrivateTemporaryDirectory {
 public:
  PrivateTemporaryDirectory(const std::string& parent, std::string_view prefix);
  ~PrivateTemporaryDirectory();
  PrivateTemporaryDirectory(const PrivateTemporaryDirectory&) = delete;
  PrivateTemporaryDirectory& operator=(const PrivateTemporaryDirectory&) = delete;

  bool valid() const { return !path_.empty(); }
  const std::string& path() const { return path_; }

 private:
  std::string path_;
};

}
}
