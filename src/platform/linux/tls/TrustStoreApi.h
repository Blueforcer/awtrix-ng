#pragma once

#include <functional>
#include "platform/linux/tls/TrustStore.h"

namespace httplib { struct Request; struct Response; }

namespace awtrix::tls {
class TrustStoreApi {
 public:
  TrustStoreApi(TrustStore& store, std::function<void(const std::string&)> changed)
      : store_(store), changed_(std::move(changed)) {}
  bool handle(const httplib::Request& request, httplib::Response& response);
 private:
  TrustStore& store_;
  std::function<void(const std::string&)> changed_;
};
}
