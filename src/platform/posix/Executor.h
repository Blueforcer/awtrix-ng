#pragma once

#include <functional>

namespace awtrix::posix {

class Executor {
 public:
  virtual ~Executor() = default;
  virtual bool post(std::function<void()> task) = 0;
};

}
