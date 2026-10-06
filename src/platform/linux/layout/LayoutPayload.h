#pragma once

#include "platform/linux/layout/Layout.h"
#include "core/payload/PayloadParser.h"

namespace awtrix::layout {

class LayoutPayload {
 public:
  LayoutPayload(DisplayProfile display, const Resources& resources, std::shared_ptr<Budget> budget);
  const payload::KeyHandler* handlers() const { return display_.height > 8 ? handlers_ : nullptr; }

 private:
  static bool validate(api::JsonReader root, bool notification, DispatchDetail& error);
  static bool read(void* context, api::JsonReader value, AppSpec& spec, DispatchDetail& error);
  DisplayProfile display_;
  Resources resources_;
  std::shared_ptr<Budget> budget_;
  payload::KeyHandler handlers_[2];
};

}
