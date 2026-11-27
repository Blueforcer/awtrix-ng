#include "platform/host/app/StatusFetch.h"

#include <ctime>
#include <utility>

#include "platform/linux/host/vendor/httplib.h"

namespace awtrix::hostapp {

bool fetchStatus(int port, std::string& body, int timeoutMs) {
  httplib::Client client("127.0.0.1", port);
  const time_t seconds = timeoutMs / 1000;
  const time_t micros = static_cast<time_t>(timeoutMs % 1000) * 1000;
  client.set_connection_timeout(seconds, micros);
  client.set_read_timeout(seconds, micros);
  client.set_write_timeout(seconds, micros);
  client.set_keep_alive(false);
  httplib::Result result = client.Get("/api/v1/status");
  if (!result || result->status != 200) return false;
  body = std::move(result->body);
  return true;
}

}
