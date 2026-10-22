#include "platform/tc002/daemon/HardwareId.h"

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>

#include "platform/posix/sha256.h"

namespace awtrix {
namespace tc002d {

namespace {

// _IOW('o', 3, int) with the key "ZkSe", then _IOR('o', 5, 18 bytes): the two requests the
// vendor's SecurityManager::getDevID() sends. The read fills 16 id bytes and returns 16.
constexpr unsigned long kUnlockRequest = 0x40046f03UL;
constexpr unsigned long kReadRequest = 0x80126f05UL;
constexpr unsigned char kUnlockKey[4] = {'Z', 'k', 'S', 'e'};

bool uniform(const UniqueId& id, std::uint8_t value) {
  return std::all_of(id.begin(), id.end(), [value](std::uint8_t b) { return b == value; });
}

}

bool readUniqueId(const std::string& device, UniqueId& id, std::string& error) {
  const int fd = ::open(device.c_str(), O_RDWR | O_CLOEXEC);
  if (fd < 0) {
    error = device + ": " + std::strerror(errno);
    return false;
  }
  unsigned char key[sizeof kUnlockKey];
  std::memcpy(key, kUnlockKey, sizeof key);
  unsigned char reply[18] = {};
  int read = -1;
  if (::ioctl(fd, kUnlockRequest, key) != 0) error = std::string("unlock: ") + std::strerror(errno);
  else if ((read = ::ioctl(fd, kReadRequest, reply)) < 0) error = std::string("read: ") + std::strerror(errno);
  else if (read != static_cast<int>(id.size())) error = "read: " + std::to_string(read) + " bytes instead of 16";
  ::close(fd);
  if (read != static_cast<int>(id.size())) return false;
  std::copy_n(reply, id.size(), id.begin());
  return true;
}

std::string activationId(const UniqueId& id) {
  if (uniform(id, 0x00) || uniform(id, 0xFF)) return {};
  sha256_state state;
  sha256_init(&state);
  sha256_update(&state, kActivationLabel, sizeof kActivationLabel - 1);
  sha256_update(&state, id.data(), id.size());
  std::uint8_t digest[32];
  sha256_final(&state, digest);
  char hex[65];
  sha256_hex(digest, hex);
  return hex;
}

}
}
