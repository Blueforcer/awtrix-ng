#pragma once

#include <cstdint>

#include "core/render/Canvas.h"

namespace awtrix {

struct Settings;
struct AppSpec;

// Shows pushed apps and notifications larger than they were made: the pipeline draws such a page
// on stage() and present() enlarges it onto the frame.
class IPageZoom {
 public:
  virtual ~IPageZoom() = default;
  // assets changes whenever icon files may have been replaced.
  virtual bool enlarges(const Settings& settings, const AppSpec& page, uint32_t assets) = 0;
  virtual Canvas& stage() = 0;
  virtual void present(Canvas& frame) const = 0;
};

}
