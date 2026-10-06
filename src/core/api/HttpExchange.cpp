#include "core/api/HttpExchange.h"

namespace awtrix::api {

std::string Request::parameter(const char* name, const char* fallback) const {
  std::string value;
  return query && query(name, value) ? value : std::string(fallback);
}

}
