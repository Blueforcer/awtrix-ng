#include "platform/linux/host/HostStore.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <new>
#include <fcntl.h>
#include <sys/stat.h>

#include <unistd.h>
#include "platform/posix/Files.h"

#include "persistence/Filesystem.h"
#include "persistence/VfsFile.h"

namespace awtrix::host {
namespace {
namespace stdfs = std::filesystem;
std::mutex writeMutex;
std::atomic<unsigned long> temporarySequence{0};

std::string canonicalDirectory(const std::string& path) try {
  if (path.empty() || path.find('\0') != std::string::npos) return {};
  std::error_code ec;
  const auto absolute = stdfs::absolute(stdfs::u8path(path), ec);
  if (ec) return {};
  const auto canonical = stdfs::weakly_canonical(absolute, ec);
  return ec ? std::string() : canonical.u8string();
} catch (const stdfs::filesystem_error&) {
  return {};
}

std::string g_dataDir = canonicalDirectory("simdata");
uint64_t g_uploadReserve = 0;

bool roomAbove(uint64_t floor, uint64_t& room) {
  std::error_code ec;
  const auto space = stdfs::space(stdfs::u8path(g_dataDir), ec);
  if (ec || space.available == static_cast<std::uintmax_t>(-1)) return false;
  room = space.available > floor ? space.available - floor : 0;
  return true;
}

bool roomAboveReserve(uint64_t& room) { return roomAbove(g_uploadReserve, room); }

// Reject traversal before normalization and symlinks below the configured root.
// Canonical containment also catches links that point outside the root.
bool containedPath(const std::string& input, stdfs::path& result) {
  if (g_dataDir.empty() || input.empty() || input.find('\0') != std::string::npos) return false;
  std::error_code ec;
  auto path = stdfs::u8path(input);
  for (const auto& component : path) if (component == "..") return false;
  path = stdfs::absolute(path, ec).lexically_normal();
  if (ec) return false;
  const auto root = stdfs::u8path(g_dataDir);
  if (!contains(root, path)) return false;
  auto cursor = root;
  for (const auto& component : path.lexically_relative(root)) {
    if (component == ".") continue;
    cursor /= component;
    const auto status = stdfs::symlink_status(cursor, ec);
    if (ec == std::errc::no_such_file_or_directory) ec.clear();
    else if (ec || stdfs::is_symlink(status)) return false;
  }
  const auto canonical = stdfs::weakly_canonical(path, ec);
  if (ec || !contains(root, canonical)) return false;
  result = canonical;
  return true;
}

bool readBounded(const stdfs::path& path, std::string& out, std::size_t maxBytes) {
  std::error_code ec;
  if (!stdfs::is_regular_file(path, ec) || ec) return false;
  const auto size = stdfs::file_size(path, ec);
  if (ec || size > maxBytes || size > kFsTotalBytes) return false;
  std::ifstream file(path, std::ios::binary);
  if (!file) return false;
  std::string value(static_cast<std::size_t>(size), '\0');
  if (size && !file.read(&value[0], static_cast<std::streamsize>(size))) return false;
  char extra;
  if (file.get(extra) || !file.eof()) return false;
  out = std::move(value);
  return true;
}

bool usedBytes(uint64_t& used) {
  used = 0;
  std::error_code ec;
  stdfs::recursive_directory_iterator iterator(stdfs::u8path(g_dataDir), ec), end;
  if (ec) return false;
  while (iterator != end) {
    const auto status = iterator->symlink_status(ec);
    if (ec) return false;
    if (stdfs::is_regular_file(status)) {
      const auto size = iterator->file_size(ec);
      if (ec || used > std::numeric_limits<uint64_t>::max() - size) return false;
      used += size;
    }
    iterator.increment(ec);
    if (ec) return false;
  }
  return true;
}

}

bool contains(const stdfs::path& root, const stdfs::path& candidate) {
  auto child = candidate.begin();
  for (auto part = root.begin(); part != root.end(); ++part, ++child)
    if (child == candidate.end() || *part != *child) return false;
  return true;
}

void setDataDir(const std::string& dir) { g_dataDir = canonicalDirectory(dir); }
const std::string& dataDir() { return g_dataDir; }

std::string hostPath(const std::string& devicePath) try {
  if (g_dataDir.empty() || devicePath.find('\0') != std::string::npos ||
      devicePath.find('\\') != std::string::npos || devicePath.find(':') != std::string::npos ||
      devicePath.rfind("//", 0) == 0) return {};
  const auto relative = stdfs::u8path(devicePath).relative_path();
  stdfs::path result;
  return containedPath((stdfs::u8path(g_dataDir) / relative).u8string(), result) ? result.u8string() : std::string();
} catch (const stdfs::filesystem_error&) {
  return {};
}

bool readFile(const std::string& path, std::string& out, std::size_t maxBytes) try {
  stdfs::path contained;
  return containedPath(path, contained) && readBounded(contained, out, maxBytes);
} catch (const stdfs::filesystem_error&) {
  return false;
}

bool readTrustedFile(const std::string& path, std::string& out, std::size_t maxBytes) try {
  return !path.empty() && path.find('\0') == std::string::npos && readBounded(stdfs::u8path(path), out, maxBytes);
} catch (const stdfs::filesystem_error&) {
  return false;
}

namespace {
bool writeContained(const std::string& path, const std::string& data, bool upload) try {
  std::lock_guard<std::mutex> lock(writeMutex);
  stdfs::path target;
  if (data.size() > kFsTotalBytes || !containedPath(path, target)) return false;
  std::error_code ec;
  if (!stdfs::is_directory(target.parent_path(), ec) || ec) return false;
  uint64_t previous = 0, used = 0, room = 0;
  if (stdfs::exists(target, ec)) {
    if (ec || !stdfs::is_regular_file(target, ec) || ec) return false;
    previous = stdfs::file_size(target, ec);
    if (ec) return false;
  } else if (ec) return false;
  if (!usedBytes(used) || used > kFsTotalBytes || previous > used ||
      data.size() > kFsTotalBytes - (used - previous)) return false;
  if (upload && g_uploadReserve && (!roomAboveReserve(room) || data.size() > room)) return false;

  stdfs::path temporary;
  int descriptor = -1;
  for (int attempt = 0; attempt < 16; ++attempt) {
    temporary = target.parent_path() / (".awtrix-tmp-" + std::to_string(++temporarySequence));
    descriptor = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (descriptor >= 0 || errno != EEXIST) break;
  }
  if (descriptor < 0) return false;
  bool ok = posix::writeAll(descriptor, data.data(), data.size());
  if (ok) ok = ::fsync(descriptor) == 0;
  if (::close(descriptor) != 0) ok = false;
  if (ok) {
    stdfs::rename(temporary, target, ec);
    ok = !ec;
  }
  if (ok) {
    const int parent = ::open(target.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    ok = parent >= 0;
    if (parent >= 0) {
      ok = ::fsync(parent) == 0;
      if (::close(parent) != 0) ok = false;
    }
  }
  stdfs::remove(temporary, ec);
  return ok;
} catch (const stdfs::filesystem_error&) {
  return false;
}
}

bool writeFile(const std::string& path, const std::string& data) {
  return writeContained(path, data, false);
}

bool writeUpload(const std::string& path, const std::string& data) {
  return writeContained(path, data, true);
}

void setUploadReserve(uint64_t bytes) { g_uploadReserve = bytes; }
uint64_t uploadReserve() { return g_uploadReserve; }

uint64_t stateRoom() {
  if (!g_uploadReserve) return kFsTotalBytes;
  uint64_t room = 0;
  roomAbove(g_uploadReserve / 4, room);
  return room;
}

uint64_t storageCapacity(uint64_t usedBytes) {
  if (!g_uploadReserve) return kFsTotalBytes;
  uint64_t room = 0;
  roomAboveReserve(room);
  return std::min(kFsTotalBytes, usedBytes + room);
}
}

namespace awtrix::fs {
bool usage(std::size_t& totalBytes, std::size_t& usedBytes) {
  uint64_t used = 0;
  const bool ok = host::usedBytes(used) && used <= std::numeric_limits<std::size_t>::max();
  totalBytes = ok ? static_cast<std::size_t>(host::kFsTotalBytes) : 0;
  usedBytes = ok ? static_cast<std::size_t>(used) : 0;
  return ok;
}

std::string vfsPath(const std::string& path) { return host::hostPath(path); }

int openRead(std::string_view path) try {
  const auto file = std::filesystem::u8path(vfsPath(std::string(path)));
  std::error_code error;
  if (file.empty() || !std::filesystem::is_regular_file(file, error) || error) {
    errno = ENOENT;
    return -1;
  }
  return ::open(file.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
} catch (const std::bad_alloc&) {
  errno = ENOMEM;
  return -1;
} catch (const std::filesystem::filesystem_error&) {
  errno = EINVAL;
  return -1;
}

long fileSize(const std::string& path) try {
  const auto file = std::filesystem::u8path(vfsPath(path));
  std::error_code error;
  if (file.empty() || !std::filesystem::is_regular_file(file, error) || error) return -1;
  const auto size = std::filesystem::file_size(file, error);
  return error || size > static_cast<uintmax_t>(std::numeric_limits<long>::max())
      ? -1 : static_cast<long>(size);
} catch (const std::filesystem::filesystem_error&) {
  return -1;
}

bool isFile(const std::string& path) { return fileSize(path) >= 0; }

bool begin() {
  namespace stdfs = std::filesystem;
  for (const char* directory : {"", "/ICONS", "/PALETTES", "/MELODIES", "/SCRIPTS"}) {
    const auto path = host::hostPath(directory);
    if (path.empty()) return false;
    std::error_code ec;
    stdfs::create_directories(stdfs::u8path(path), ec);
    if (ec) return false;
  }
  return true;
}
}
