#include "platform/tc002/daemon/ip/ClientOutput.h"

namespace awtrix {
namespace tc002d {
namespace ip {
namespace {

constexpr std::string_view kRoutine[] = {
    "started, v",
    "broadcasting discover",
    "broadcasting select for ",
    "sending renew to server ",
    "broadcasting renew",
    "lease of ",
    "received SIGTERM",
    "sending release",
    "entering released state",
};

}

bool routineClientMessage(std::string_view line) {
  constexpr std::string_view prefix = "udhcpc: ";
  if (line.substr(0, prefix.size()) != prefix) return false;
  line.remove_prefix(prefix.size());
  for (std::string_view routine : kRoutine)
    if (line.substr(0, routine.size()) == routine) return true;
  return false;
}

void ClientOutput::feed(const char* data, std::size_t size, int64_t nowMs) {
  for (std::size_t at = 0; at < size; ++at) {
    const char c = data[at];
    if (c == '\n') {
      line(std::move(pending_), nowMs);
      pending_.clear();
    } else if (pending_.size() < kMaxLine) {
      pending_ += c;
    }
  }
}

void ClientOutput::finish(int64_t nowMs) {
  if (!pending_.empty()) line(std::move(pending_), nowMs);
  pending_.clear();
  if (suppressed_) {
    emit_(std::to_string(suppressed_) + " more lines suppressed");
    suppressed_ = 0;
  }
}

void ClientOutput::line(std::string text, int64_t nowMs) {
  while (!text.empty() && (text.back() == '\r' || text.back() == ' ')) text.pop_back();
  if (text.empty() || routineClientMessage(text)) return;
  if (windowStartMs_ < 0 || nowMs - windowStartMs_ >= windowMs_) {
    if (suppressed_) emit_(std::to_string(suppressed_) + " more lines suppressed");
    windowStartMs_ = nowMs;
    emitted_ = suppressed_ = 0;
  }
  if (emitted_ >= budget_) {
    ++suppressed_;
    return;
  }
  ++emitted_;
  constexpr std::string_view prefix = "udhcpc: ";
  if (text.compare(0, prefix.size(), prefix) == 0) text.erase(0, prefix.size());
  emit_(text);
}

}
}
}
