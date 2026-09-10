#pragma once

#include "core/icons/IconOrigins.h"

namespace awtrix::iconorigins {
// Platform-specific filesystem adapter; no in-memory cache survives a request.
Backend& storage();
}
