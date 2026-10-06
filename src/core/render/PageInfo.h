#pragma once

#include <cstdint>
#include <string>

#include "core/render/Canvas.h"

namespace awtrix {

// What the render pipeline put on the panel this frame. Notifications interrupt everything; an
// external page, while it is active, takes the place of the app rotation.
enum class PageKind : uint8_t { App, Notification, External };

struct PageInfo {
  PageKind kind = PageKind::App;
  // The app on screen, and the one sliding in while a transition runs. Both are empty for a
  // notification or an external page.
  std::string app;
  std::string incoming;
};

// A page drawn from outside the app rotation, such as the display of another clock.
class IExternalPage {
 public:
  virtual ~IExternalPage() = default;
  virtual bool active() const = 0;
  virtual void draw(Canvas& canvas) const = 0;
};

// Receives every composed frame before the power fade and the on-panel menus are applied. page is
// null while the pipeline did not draw the frame: display off, moodlight or a platform takeover.
class IContentSink {
 public:
  virtual ~IContentSink() = default;
  virtual void content(const Canvas& frame, const PageInfo* page, int64_t nowMs) = 0;
};

}
