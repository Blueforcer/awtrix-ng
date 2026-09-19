#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "core/script/ScriptServices.h"

namespace awtrix::script::modbus {

struct Read {
  std::string host;
  uint16_t port = 502;
  uint8_t unit = 1;
  uint8_t function = 3;
  uint16_t address = 0;
  uint16_t count = 1;
};

bool isUrl(std::string_view url);
bool parse(std::string_view url, Read& out);
void encode(const Read& req, uint16_t transaction, uint8_t (&out)[12]);
HttpResult decode(const Read& req, uint32_t id, const uint8_t* frame, std::size_t size);

template <typename Client, typename Clock, typename Yield>
HttpResult exchange(Client& client, const Read& req, uint32_t id, Clock now, Yield pause) {
  HttpResult failed;
  failed.id = id;
  uint8_t request[12];
  encode(req, static_cast<uint16_t>(id), request);
  const auto start = now();
  if (client.write(request, sizeof(request)) != sizeof(request)) return failed;
  uint8_t frame[260];
  std::size_t used = 0, wanted = 7;
  while (used < wanted) {
    if (now() - start >= 2000) return failed;
    const int available = client.available();
    if (available > 0) {
      const std::size_t n = static_cast<std::size_t>(available) < wanted - used
                                ? static_cast<std::size_t>(available) : wanted - used;
      const int got = client.read(frame + used, n);
      if (got <= 0 || static_cast<std::size_t>(got) > n) return failed;
      used += static_cast<std::size_t>(got);
      if (used == 7 && wanted == 7) {
        const unsigned length = (unsigned(frame[4]) << 8) | frame[5];
        if (length < 3 || length > 254) return failed;
        wanted = 6 + length;
      }
    } else {
      if (!client.connected()) return failed;
      pause();
    }
  }
  return decode(req, id, frame, used);
}

}
