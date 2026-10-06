#include "platform/tc002/contract/InputIdentity.h"

#include <array>
#include <cstring>
#include <initializer_list>
#include <linux/input.h>
#include <sys/ioctl.h>

#include "platform/tc002/contract/tc002_layout.h"

namespace awtrix::tc002 {
namespace {

template <std::size_t Bytes>
bool exactBits(const std::array<unsigned char, Bytes>& bits, std::initializer_list<unsigned> wanted) {
  std::array<unsigned char, Bytes> expected{};
  for (const unsigned bit : wanted) {
    if (bit / 8 >= Bytes) return false;
    expected[bit / 8] |= static_cast<unsigned char>(1U << (bit % 8));
  }
  return bits == expected;
}

}

InputKind inputKind(int fd) {
  char name[128]{};
  input_id id{};
  std::array<unsigned char, (EV_MAX + 8) / 8> events{};
  std::array<unsigned char, (KEY_MAX + 8) / 8> keys{};
  std::array<unsigned char, (ABS_MAX + 8) / 8> absolute{};
  if (::ioctl(fd, EVIOCGNAME(sizeof name - 1), name) < 0 || ::ioctl(fd, EVIOCGID, &id) < 0 ||
      ::ioctl(fd, EVIOCGBIT(0, events.size()), events.data()) < 0 ||
      ::ioctl(fd, EVIOCGBIT(EV_KEY, keys.size()), keys.data()) < 0 ||
      ::ioctl(fd, EVIOCGBIT(EV_ABS, absolute.size()), absolute.data()) < 0 || id.bustype != 0x19)
    return InputKind::Other;
  if (!std::strcmp(name, TC002_KEYS_DEVICE_NAME) && id.vendor == 1 && id.product == 1 && id.version == 0x0100 &&
      exactBits(events, {EV_SYN, EV_KEY}) && exactBits(keys, {KEY_UP, KEY_LEFT, KEY_RIGHT, KEY_DOWN}) &&
      exactBits(absolute, {}))
    return InputKind::Keys;
  if (!std::strcmp(name, TC002_KNOB_DEVICE_NAME) && id.vendor == 0xdead && id.product == 0xbeef && id.version == 0x28bb &&
      exactBits(events, {EV_SYN, EV_ABS}) && exactBits(keys, {}) && exactBits(absolute, {ABS_X}))
    return InputKind::Knob;
  return InputKind::Other;
}

}
