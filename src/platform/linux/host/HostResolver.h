#pragma once

#include <memory>

#include "transport/net/HostResolver.h"

namespace awtrix {
namespace net {

// A host resolver whose every lookup takes at least delayMs, so tests can reach the pending path.
std::unique_ptr<IHostResolver> makeSlowHostResolver(long delayMs);

}
}
