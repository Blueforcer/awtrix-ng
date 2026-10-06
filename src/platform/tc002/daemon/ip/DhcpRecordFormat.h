#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

// One udhcpc event as a single line on the daemon's private pipe:
//   v=1<TAB>event=bound<TAB>mono=<CLOCK_MONOTONIC ms><TAB>ip=...<TAB>...\n
// Values are copied from udhcpc's environment; only printable ASCII survives, anything else
// drops the field. The daemon validates the content.
namespace awtrix {
namespace tc002d {
namespace ip {

constexpr int kDhcpEventFd = 198;
constexpr std::size_t kMaxDhcpRecord = 2048;
constexpr std::size_t kMaxDhcpValue = 255;
constexpr const char* kDhcpForwardedFields[] = {"interface", "ip",    "subnet", "router",
                                                "dns",       "broadcast", "lease", "ntpsrv"};

inline bool dhcpRecordValue(const char* value) {
  if (!value || !*value) return false;
  std::size_t size = 0;
  for (; value[size]; ++size) {
    const unsigned char c = static_cast<unsigned char>(value[size]);
    if (size >= kMaxDhcpValue || c < 0x20 || c > 0x7e) return false;
  }
  return true;
}

inline bool appendDhcpField(char* out, std::size_t capacity, std::size_t& size, const char* key,
                            const char* value) {
  const int n = std::snprintf(out + size, capacity - size, "%s%s=%s", size ? "\t" : "", key, value);
  if (n < 0 || static_cast<std::size_t>(n) >= capacity - size) return false;
  size += static_cast<std::size_t>(n);
  return true;
}

template <typename Lookup>
std::size_t formatDhcpRecord(const char* event, int64_t capturedMs, Lookup lookup, char* out,
                             std::size_t capacity) {
  if (!dhcpRecordValue(event) || capacity < 2) return 0;
  std::size_t size = 0;
  char mono[24];
  std::snprintf(mono, sizeof(mono), "%lld", static_cast<long long>(capturedMs));
  if (!appendDhcpField(out, capacity - 1, size, "v", "1") ||
      !appendDhcpField(out, capacity - 1, size, "event", event) ||
      (capturedMs >= 0 && !appendDhcpField(out, capacity - 1, size, "mono", mono)))
    return 0;
  for (const char* key : kDhcpForwardedFields) {
    const char* value = lookup(key);
    if (!dhcpRecordValue(value)) continue;
    if (!appendDhcpField(out, capacity - 1, size, key, value)) return 0;
  }
  out[size++] = '\n';
  out[size] = '\0';
  return size;
}

}
}
}
