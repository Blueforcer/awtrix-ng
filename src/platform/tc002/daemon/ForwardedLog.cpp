#include "platform/tc002/daemon/ForwardedLog.h"

#include "platform/tc002/daemon/Log.h"
#include "platform/tc002/daemon/RuntimeChild.h"

namespace awtrix::tc002d {

ForwardedLog::ForwardedLog() {
  Log::forwardTo([this](const char* component, std::string_view text) { forward(component, text); });
}

ForwardedLog::~ForwardedLog() { Log::forwardTo(nullptr); }

ForwardedLog::Attachment ForwardedLog::attach(RuntimeChild& runtime) {
  runtime_ = &runtime;
  for (const tc002::LogLine& line : early_) runtime.logLine(line.component.c_str(), line.text);
  early_.clear();
  return Attachment(*this);
}

void ForwardedLog::forward(const char* component, std::string_view text) {
  if (runtime_) {
    runtime_->logLine(component, text);
    return;
  }
  if (!RuntimeLog::forwarded(component)) return;
  if (early_.size() == RuntimeLog::kBacklog) early_.pop_front();
  early_.push_back({component, std::string(text)});
}
}  // namespace awtrix::tc002d
