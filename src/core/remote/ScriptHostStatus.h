#pragma once

#include <string>
#include <vector>

#include "core/remote/OfferList.h"

namespace awtrix {
namespace remote {

// What GET /api/v1/scripthost reports.
struct ScriptHostStatus {
  // host[:port] while paired, empty otherwise.
  std::string host;
  std::string key;
  // off, resolving, notFound, down or up.
  const char* link = "off";
  std::vector<OfferEntry> offers;
};

std::string scriptHostJson(const ScriptHostStatus& status);

}
}
