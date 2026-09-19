#include "core/script/ModbusTcp.h"

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

HttpResult decode(const Read& r, uint32_t id, const uint8_t* f, std::size_t size) {
  HttpResult result;
  result.id = id;
  if (size < 9 || size > 260 || word(f) != uint16_t(id) || word(f + 2) != 0 ||
      word(f + 4) != size - 6 || f[6] != r.unit) return result;
  if (f[7] == (r.function | 0x80)) {
    if (size == 9 && f[8] != 0) result.status = f[8];
    return result;
  }
  const unsigned bytes = r.function <= 2 ? (r.count + 7) / 8 : r.count * 2;
  if (f[7] != r.function || f[8] != bytes || size != 9 + bytes) return result;
  const std::size_t cap = 2 + r.count * (r.function <= 2 ? 2u : 6u);
  if (heap::growthBudget() < cap) return result;
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
}
