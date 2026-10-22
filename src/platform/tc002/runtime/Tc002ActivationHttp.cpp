#include "platform/tc002/runtime/Tc002ActivationHttp.h"

#include "platform/linux/host/vendor/httplib.h"
#include "platform/tc002/runtime/Tc002Activation.h"

namespace awtrix {

int Tc002ActivationHttp::post(const std::string& body) {
  std::string origin, path;
  if (!net::splitUrl(Tc002Activation::kUrl, origin, path)) return 0;
  httplib::Client client(origin);
  net::configure(client, {15000, 5, false});
  interrupt_.watch(client);
  const auto response = net::send(client, "POST", path, body, {{"Content-Type", "application/json"}},
                                 [](const char*, std::size_t) { return true; });
  interrupt_.release();
  return response ? response->status : 0;
}

}
