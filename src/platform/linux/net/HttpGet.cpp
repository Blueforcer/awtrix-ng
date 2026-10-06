#include "platform/linux/net/HttpGet.h"

#include "platform/linux/host/vendor/httplib.h"

namespace awtrix::net {

HttpGet::Result HttpGet::fetch(const std::string& url, const Limits& limits, std::string& body) {
  body.clear();
  Result result;
  std::string origin, target;
  if (!splitUrl(url, origin, target)) return result;
  httplib::Client client(origin);
  configure(client, {limits.timeoutMs, 3, limits.followRedirects});
  interrupt_.watch(client);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (aborted_) {
      result.failure = Failure::Aborted;
      return result;
    }
    interrupt_.rearm();
  }

  ReceivedBody received;
  const auto response = receive200(client, target, {limits.maxBytes, {}, {}}, received,
                                   [&](const char* bytes, std::size_t count) {
    body.append(bytes, count);
    return true;
  });
  result.status = received.status;
  interrupt_.release();
  bool aborted = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    aborted = aborted_;
  }
  if (aborted) result.failure = Failure::Aborted;
  else if (received.tooLarge) result.failure = Failure::TooLarge;
  else if (result.status != 0 && result.status != 200) result.failure = Failure::Status;
  else if (response && response->status == 200) result.failure = Failure::None;
  if (result.failure != Failure::None) body.clear();
  return result;
}

void HttpGet::abort() {
  std::lock_guard<std::mutex> lock(mutex_);
  aborted_ = true;
  interrupt_.interrupt();
}

}
