#include "platform/posix/Bytes.h"
#include "platform/posix/Files.h"
#include "platform/tc002/daemon/wifi/CredentialStore.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace awtrix {
namespace tc002d {
namespace wifi {
namespace {

constexpr char kMagic[8] = {'A', 'W', 'N', 'G', 'W', 'I', 'F', 'I'};
constexpr uint8_t kVersion = 1;
constexpr uint8_t kOpenFlag = 1;
constexpr std::size_t kHeaderBytes = 16;
constexpr std::size_t kDigestBytes = 20;
constexpr std::size_t kMaxRecord = kHeaderBytes + kMaxSsidBytes + 32 + kDigestBytes;


}

const char* storeStatusName(StoreStatus status) {
  switch (status) {
    case StoreStatus::Loaded: return "ok";
    case StoreStatus::Missing: return "missing";
    case StoreStatus::Invalid: return "invalid";
    case StoreStatus::Unsafe: return "unsafe";
    case StoreStatus::IoError: return "error";
  }
  return "error";
}

bool makeProfile(std::string_view ssid, std::string_view password, WifiProfile& out) {
  if (ssid.empty() || ssid.size() > kMaxSsidBytes) return false;
  WifiProfile profile;
  profile.ssid.assign(ssid.data(), ssid.size());
  if (password.empty()) {
    profile.open = true;
  } else if (tc002::wifiHexKey(password)) {
    for (std::size_t i = 0; i < profile.psk.size(); ++i)
      profile.psk[i] = uint8_t(posix::hexValue(password[2 * i]) << 4 | posix::hexValue(password[2 * i + 1]));
  } else if (tc002::wifiPassphrase(password)) {
    profile.psk = deriveWpaPsk(password, ssid);
  } else {
    return false;
  }
  out = profile;
  return true;
}

CredentialStore::CredentialStore(std::string directory) : directory_(std::move(directory)) {}

std::string CredentialStore::encode(const WifiProfile& profile) {
  if (profile.ssid.empty() || profile.ssid.size() > kMaxSsidBytes) return {};
  const std::size_t pskBytes = profile.open ? 0 : profile.psk.size();
  std::string record(kMagic, sizeof kMagic);
  record += char(kVersion);
  record += char(profile.open ? kOpenFlag : 0);
  record += char(profile.ssid.size());
  record += char(pskBytes);
  record.append(4, '\0');
  record += profile.ssid;
  record.append(reinterpret_cast<const char*>(profile.psk.data()), pskBytes);
  const Sha1Digest digest = sha1(record.data(), record.size());
  record.append(reinterpret_cast<const char*>(digest.data()), digest.size());
  return record;
}

bool CredentialStore::decode(std::string_view record, WifiProfile& out) {
  if (record.size() < kHeaderBytes + 1 + kDigestBytes || record.size() > kMaxRecord) return false;
  const auto byte = [&](std::size_t at) { return static_cast<uint8_t>(record[at]); };
  if (std::memcmp(record.data(), kMagic, sizeof kMagic) != 0 || byte(8) != kVersion) return false;
  const uint8_t flags = byte(9);
  const std::size_t ssidBytes = byte(10), pskBytes = byte(11);
  const bool open = flags == kOpenFlag;
  if ((flags & ~kOpenFlag) || !ssidBytes || ssidBytes > kMaxSsidBytes ||
      pskBytes != (open ? 0u : 32u) || byte(12) || byte(13) || byte(14) || byte(15) ||
      record.size() != kHeaderBytes + ssidBytes + pskBytes + kDigestBytes)
    return false;
  const std::size_t body = record.size() - kDigestBytes;
  const Sha1Digest digest = sha1(record.data(), body);
  if (std::memcmp(digest.data(), record.data() + body, kDigestBytes) != 0) return false;
  WifiProfile profile;
  profile.ssid.assign(record.data() + kHeaderBytes, ssidBytes);
  profile.open = open;
  if (!open) std::memcpy(profile.psk.data(), record.data() + kHeaderBytes + ssidBytes, pskBytes);
  out = profile;
  return true;
}

int CredentialStore::openDirectory(bool create, int& error) const {
  if (directory_.size() < 2 || directory_[0] != '/') { error = EINVAL; return -1; }
  if (create && !posix::makeDirectories(posix::parentDirectory(directory_), 0755)) {
    error = errno;
    return -1;
  }
  auto directory = posix::openPrivateDirectory(directory_, create);
  if (!directory.valid()) error = errno;
  return directory.release();
}

StoreStatus CredentialStore::load(WifiProfile& out) const {
  int error = 0;
  const int directory = openDirectory(false, error);
  if (directory < 0) {
    if (error == ENOENT) return StoreStatus::Missing;
    return error == EPERM || error == ELOOP || error == ENOTDIR ? StoreStatus::Unsafe : StoreStatus::IoError;
  }
  const int fd = openat(directory, kFileName, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
  const int openError = errno;
  close(directory);
  if (fd < 0) {
    if (openError == ENOENT) return StoreStatus::Missing;
    return openError == ELOOP ? StoreStatus::Unsafe : StoreStatus::IoError;
  }
  struct stat info {};
  if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_uid != geteuid() ||
      (info.st_mode & 07777) != 0600 || info.st_nlink != 1 || info.st_size > off_t(kMaxRecord)) {
    close(fd);
    return StoreStatus::Unsafe;
  }
  char buffer[kMaxRecord + 1];
  std::size_t size = 0;
  bool readOk = true;
  while (size < sizeof buffer) {
    const ssize_t n = read(fd, buffer + size, sizeof buffer - size);
    if (n < 0 && errno == EINTR) continue;
    if (n < 0) { readOk = false; break; }
    if (n == 0) break;
    size += static_cast<std::size_t>(n);
  }
  close(fd);
  StoreStatus status = StoreStatus::IoError;
  if (readOk) status = decode(std::string_view(buffer, size), out) ? StoreStatus::Loaded : StoreStatus::Invalid;
  secureErase(buffer, sizeof buffer);
  return status;
}

bool CredentialStore::erase(int& error) const {
  error = 0;
  const int directory = openDirectory(false, error);
  if (directory < 0) {
    if (error != ENOENT) return false;
    error = 0;
    return true;
  }
  for (const char* name : {kFileName, kTemporaryName})
    if (unlinkat(directory, name, 0) != 0 && errno != ENOENT && !error) error = errno;
  if (fsync(directory) != 0 && !error) error = errno;
  close(directory);
  return error == 0;
}

bool CredentialStore::save(const WifiProfile& profile, int& error) const {
  error = 0;
  std::string record = encode(profile);
  if (record.empty()) { error = EINVAL; return false; }
  const int directory = openDirectory(true, error);
  bool ok = false;
  if (directory >= 0) {
    if (unlinkat(directory, kTemporaryName, 0) != 0 && errno != ENOENT) error = errno;
    const int fd = error ? -1
        : openat(directory, kTemporaryName, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd >= 0) {
      ok = fchmod(fd, 0600) == 0 && posix::writeAll(fd, record.data(), record.size()) && fsync(fd) == 0;
      if (!ok) error = errno;
      if (close(fd) != 0 && ok) { ok = false; error = errno; }
      if (ok && renameat(directory, kTemporaryName, directory, kFileName) != 0) { ok = false; error = errno; }
      if (ok && fsync(directory) != 0) { ok = false; error = errno; }
      if (!ok) unlinkat(directory, kTemporaryName, 0);
    } else if (!error) {
      error = errno;
    }
    close(directory);
  }
  secureErase(&record[0], record.size());
  if (!ok && !error) error = EIO;
  return ok;
}

}
}
}
