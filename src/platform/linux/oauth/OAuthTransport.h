#pragma once

#include "platform/linux/net/HttpClient.h"
#include "platform/linux/oauth/OAuthService.h"

namespace awtrix::oauth {

// HTTPS with verified certificates, no redirects, 30 s from connecting to the last byte.
class HttplibTransport {
 public:
  HttpReply operator()(const HttpCall& call);
  void interrupt() { interrupt_.interrupt(); }

 private:
  net::SocketInterrupt interrupt_;
};

}
