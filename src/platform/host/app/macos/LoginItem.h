#pragma once

#include <optional>

namespace awtrix::hostapp {

class LoginItem {
 public:
  virtual ~LoginItem() = default;
  virtual bool enabled() const = 0;
  virtual bool enable(bool on) = 0;
};

class LoginChoice {
 public:
  virtual ~LoginChoice() = default;
  virtual std::optional<bool> stored() const = 0;
  virtual void store(bool on) = 0;
};

}
