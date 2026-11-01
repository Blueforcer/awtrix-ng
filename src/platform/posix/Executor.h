#pragma once

#include <functional>

namespace awtrix::posix {

class Executor {
 public:
  virtual ~Executor() = default;
  virtual void post(std::function<void()> task) = 0;
};

}
