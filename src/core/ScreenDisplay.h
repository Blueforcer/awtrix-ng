#pragma once

#include <functional>
#include <string>
#include <utility>

#include "core/Services.h"
#include "core/api/StateJson.h"

namespace awtrix {

class ScreenDisplay : public IDisplayService {
 public:
  using Publisher = std::function<void(const std::string&, const std::string&)>;

  void setPublisher(Publisher publish) { publish_ = std::move(publish); }
  void setScreen(Canvas* screen) { screen_ = screen; }
  void sendScreen() override {
    if (publish_ && screen_) publish_("state/screen", buildScreenJson(*screen_));
  }

 private:
  Publisher publish_;
  Canvas* screen_ = nullptr;
};

}
