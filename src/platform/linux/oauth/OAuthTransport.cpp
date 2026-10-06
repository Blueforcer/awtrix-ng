#include "platform/linux/oauth/OAuthTransport.h"

#include <algorithm>

#include "platform/linux/host/vendor/httplib.h"

namespace awtrix::oauth {

HttpReply HttplibTransport::operator()(const HttpCall& call) {
  HttpReply reply;
  std::string origin, path;
  if (!net::splitUrl(call.url, origin, path) || origin.rfind("https://", 0) != 0) return reply;
  httplib::Client client(origin);
  net::configure(client, {30000, 3, false});
  interrupt_.rearm();
  interrupt_.watch(client);
  const auto response = net::send(client, call.method, path, call.body, call.headers,
                                 [&](const char* bytes, std::size_t n) {
    if (reply.body.size() < call.cap) reply.body.append(bytes, std::min(n, call.cap - reply.body.size()));
    return true;
  });
  interrupt_.release();
  if (response) reply.status = response->status;
  else reply.body.clear();
  return reply;
}

}
