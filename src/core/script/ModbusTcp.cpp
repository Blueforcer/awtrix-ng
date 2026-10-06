#include "core/script/ModbusTcp.h"

#include <cstdio>

namespace awtrix::script::modbus {
namespace {
bool number(std::string_view s, unsigned max, unsigned& out) {
  if (s.empty()) return false;
  out = 0;
  for (char c : s) {
    if (c < '0' || c > '9') return false;
    const unsigned digit = unsigned(c - '0');
    if (out > max / 10 || (out == max / 10 && digit > max % 10)) return false;
    out = out * 10 + digit;
  }
  return true;
}
unsigned word(const uint8_t* p) { return (unsigned(p[0]) << 8) | p[1]; }
}

bool isUrl(std::string_view url) { return url.substr(0, 9) == "modbus://"; }

bool parse(std::string_view url, Read& out) {
  if (!isUrl(url)) return false;
  url.remove_prefix(9);
  const auto slash = url.find('/');
  if (slash == std::string_view::npos) return false;
  auto host = url.substr(0, slash);
  unsigned port = 502;
  const auto colon = host.find(':');
  if (colon != std::string_view::npos) {
    if (!number(host.substr(colon + 1), 65535, port) || port == 0) return false;
    host = host.substr(0, colon);
  }
  if (host.empty() || host.size() > 253) return false;
  for (char c : host)
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '-' || c == '.')) return false;
  url.remove_prefix(slash + 1);
  unsigned fields[4];
  const unsigned limits[] = {255, 4, 65535, 2000};
  for (unsigned i = 0; i < 4; ++i) {
    const auto end = url.find('/');
    if ((i < 3) != (end != std::string_view::npos)) return false;
    if (!number(url.substr(0, end), limits[i], fields[i])) return false;
    if (i < 3) url.remove_prefix(end + 1);
  }
  if (fields[1] == 0 || fields[3] == 0 ||
      (fields[1] >= 3 && fields[3] > 125) || fields[2] + fields[3] > 65536) return false;
  out.host.assign(host);
  out.port = static_cast<uint16_t>(port);
  out.unit = static_cast<uint8_t>(fields[0]);
  out.function = static_cast<uint8_t>(fields[1]);
  out.address = static_cast<uint16_t>(fields[2]);
  out.count = static_cast<uint16_t>(fields[3]);
  return true;
}

void encode(const Read& r, uint16_t transaction, uint8_t (&out)[12]) {
  const uint8_t bytes[] = {uint8_t(transaction >> 8), uint8_t(transaction), 0, 0, 0, 6,
      r.unit, r.function, uint8_t(r.address >> 8), uint8_t(r.address),
      uint8_t(r.count >> 8), uint8_t(r.count)};
  for (unsigned i = 0; i < 12; ++i) out[i] = bytes[i];
}

HttpResult decode(const Read& r, uint32_t id, const uint8_t* f, std::size_t size,
                  const char** diagnostic) {
  HttpResult result;
  result.id = id;
  auto fail = [&](const char* reason) {
    if (diagnostic) *diagnostic = reason;
    return result;
  };
  if (diagnostic) *diagnostic = nullptr;
  if (size < 9 || size > 260) return fail("invalid response size");
  if (word(f) != uint16_t(id)) return fail("transaction mismatch");
  if (word(f + 2) != 0) return fail("invalid protocol");
  if (word(f + 4) != size - 6) return fail("invalid response length");
  if (f[6] != r.unit) return fail("unit mismatch");
  if (f[7] == (r.function | 0x80)) {
    if (size != 9 || f[8] == 0) return fail("invalid exception response");
    result.status = f[8];
    switch (f[8]) {
      case 1: return fail("unsupported function");
      case 2: return fail("illegal register address/range");
      case 3: return fail("illegal request value");
      case 4: return fail("device failure");
      case 5: return fail("device acknowledged; processing");
      case 6: return fail("device busy");
      case 10: return fail("gateway path unavailable");
      case 11: return fail("gateway target did not respond");
      default: return fail("device exception");
    }
  }
  const unsigned bytes = r.function <= 2 ? (r.count + 7) / 8 : r.count * 2;
  if (f[7] != r.function) return fail("function mismatch");
  if (f[8] != bytes || size != 9 + bytes) return fail("register/bit count mismatch");
  const std::size_t cap = 2 + r.count * (r.function <= 2 ? 2u : 6u);
  if (heap::growthBudget() < cap) return fail("not enough memory");
  result.body.reserve(cap);
  result.body = "[";
  for (unsigned i = 0; i < r.count; ++i) {
    if (i) result.body += ',';
    const unsigned value = r.function <= 2 ? (f[9 + i / 8] >> (i % 8)) & 1
                                           : word(f + 9 + i * 2);
    result.body += std::to_string(value);
  }
  result.body += ']';
  result.ok = true;
  result.status = 200;
  return result;
}

std::string failureReport(const Read& r, const char* diagnostic, int status,
                          unsigned long elapsedMs) {
  char line[96];
  std::snprintf(line, sizeof(line), " (%d, %lums) reg=%u count=%u unit=%u fc=%u host=",
                status ? status : -1, elapsedMs, unsigned(r.address), unsigned(r.count),
                unsigned(r.unit), unsigned(r.function));
  return "modbus: " + std::string(diagnostic ? diagnostic : "request failed") + line + r.host +
         ':' + std::to_string(r.port);
}
}
