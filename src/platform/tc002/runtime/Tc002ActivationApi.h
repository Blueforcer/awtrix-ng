#pragma once

#include <string>

namespace awtrix {
class Tc002Activation;

// /api/v1/activation: the web UI's question whether this clock may be counted for Ulanzi. GET
// answers {"consent": null | true | false}, null while unasked; PUT {"consent": true | false}
// keeps the answer. Runs on the main loop.
class Tc002ActivationApi {
 public:
  explicit Tc002ActivationApi(Tc002Activation& activation) : activation_(activation) {}
  // The HTTP status and JSON body, or 0 for a path that is not its own.
  int handle(const std::string& method, const std::string& path, const std::string& request, std::string& body);

 private:
  Tc002Activation& activation_;
};

}
