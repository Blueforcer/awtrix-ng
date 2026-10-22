#pragma once

#include <string>

#include "platform/linux/net/HttpClient.h"

namespace awtrix {

// The request behind Tc002Activation::Post: HTTPS with verified certificates, no redirects and
// 15 s from connecting to the last byte. interrupt() ends it for good, so it is only called once
// the activation shuts down.
class Tc002ActivationHttp {
 public:
  int post(const std::string& body);
  void interrupt() { interrupt_.interrupt(); }

 private:
  net::SocketInterrupt interrupt_;
};

}
