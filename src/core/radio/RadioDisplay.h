#pragma once

#include <string>

#include "core/payload/AppSpec.h"

namespace awtrix {
namespace radio {

inline constexpr const char kNotificationName[] = "radio";

enum class Announcement {
  Station,
  Title,
};

bool buildAnnouncement(const std::string& text, Announcement kind, AppSpec& out);

}
}
