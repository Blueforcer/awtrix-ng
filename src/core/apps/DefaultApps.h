#pragma once

#include "core/apps/AppRegistry.h"
#include "core/apps/builtin/TimeApp.h"
#include "core/apps/builtin/DateApp.h"
#include "core/apps/builtin/TempApp.h"
#include "core/apps/builtin/HumidityApp.h"
#include "core/apps/builtin/BatteryApp.h"

namespace awtrix {
class DefaultApps {
 public:
  AppRegistry registry() {
    AppRegistry apps;
    IApp* list[] = {&time_, &date_, &temperature_, &humidity_, &battery_};
    for (IApp* app : list)
      apps.add(app);
    return apps;
  }
 private:
  TimeApp time_;
  DateApp date_;
  TempApp temperature_;
  HumidityApp humidity_;
  BatteryApp battery_;
};
}
