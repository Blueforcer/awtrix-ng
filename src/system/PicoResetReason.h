#pragma once
namespace awtrix::platform {
// Use symbolic framework values, not enum ordinals. Debug/RUN resets map to
// the existing external value rather than adding a platform-only API string.
template <typename Reason>
constexpr const char* picoResetReasonName(Reason reason) {
  switch (reason) {
    case Reason::PWRON_RESET: return "poweron";
    case Reason::RUN_PIN_RESET: return "external";
    case Reason::SOFT_RESET: return "software";
    case Reason::WDT_RESET: return "watchdog";
    case Reason::DEBUG_RESET: return "external";
    case Reason::BROWNOUT_RESET: return "brownout";
    default: return "unknown";
  }
}
}
