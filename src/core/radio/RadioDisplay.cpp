#include "core/radio/RadioDisplay.h"

#include "core/render/TextEncoding.h"

namespace awtrix {
namespace radio {

// Both announcement kinds share one notification name and do not stack, so a new station or track
// title replaces whatever the radio last put on screen instead of queueing behind it.
bool buildAnnouncement(const std::string& text, Announcement kind, AppSpec& out) {
  const std::string normalised = text::fromStreamBytes(text);
  if (normalised.empty()) return false;

  out = AppSpec{};
  out.name = kNotificationName;
  out.isNotification = true;
  out.text = normalised;
  out.durationMs = kNotificationMs;
  out.stack = false;
  out.repeat = 1;
  (void)kind;
  return true;
}

}
}
