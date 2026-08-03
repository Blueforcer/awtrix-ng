#pragma once

#include <string>

#include "core/payload/AppSpec.h"

namespace awtrix {
namespace radio {

inline constexpr const char kNotificationName[] = "radio";

inline constexpr long kNotificationMs = 7000;

enum class Announcement {
  Station,
  Title,
};

bool buildAnnouncement(const std::string& text, Announcement kind, AppSpec& out);

}
}
