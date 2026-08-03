#pragma once

#include "core/Command.h"
#include "core/Services.h"

namespace awtrix {

class Dispatcher {
 public:
  DispatchResult dispatch(const Command& cmd, CommandContext& ctx);
};

}
