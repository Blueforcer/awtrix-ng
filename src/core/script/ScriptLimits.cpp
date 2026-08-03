#include <algorithm>

#include "core/script/ScriptServices.h"


namespace awtrix::script {
namespace {
std::size_t g_maxSourceBytes = kDefaultMaxSourceBytes;
}

std::size_t maxSourceBytes() { return g_maxSourceBytes; }

void setMaxSourceBytes(std::size_t bytes) {
  g_maxSourceBytes = std::clamp(bytes, kMinMaxSourceBytes, kMaxSourceCeilingBytes);
}

}
