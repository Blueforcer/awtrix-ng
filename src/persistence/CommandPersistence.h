#pragma once

#include "core/Command.h"
#include "persistence/AppOrderStore.h"
#include "persistence/RadioStore.h"

namespace awtrix::persistence {

inline bool commandPending(CommandType type) {
  return ((type == CommandType::SetAppOrder || type == CommandType::SetAppEnabled) &&
          apporder::pending()) ||
         (type == CommandType::SetRadioStations && radiostore::pending());
}

inline bool documentsPending() { return apporder::pending() || radiostore::pending(); }

}
