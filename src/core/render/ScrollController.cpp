#include "core/render/ScrollController.h"

namespace awtrix {
namespace render {

void ScrollController::set(const ScrollSpec& spec, const ScrollDefaults& defaults,
                           const ScrollLayout& layout, int64_t nowMs) {
  const ResolvedScroll next = resolve(spec, defaults, layout);
  if (next == resolved_) return;
  resolved_ = next;
  restart(nowMs);
}

}
}
