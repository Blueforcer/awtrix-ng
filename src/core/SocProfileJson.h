#pragma once

#include <string>

#include "core/SocProfile.h"

namespace awtrix {
namespace pins {

inline std::string toJson(const SocProfile& soc) {
  auto ranges = [](const RangeList& list) {
    std::string out = "[";
    for (std::size_t i = 0; i < list.count; ++i) {
      if (i) out += ',';
      out += '[' + std::to_string(list.items[i].lo) + ',' + std::to_string(list.items[i].hi) + ']';
    }
    return out + ']';
  };
  auto reserved = [](const ReservedList& list) {
    std::string out = "[";
    for (std::size_t i = 0; i < list.count; ++i) {
      if (i) out += ',';
      out += "{\"lo\":" + std::to_string(list.items[i].lo) +
             ",\"hi\":" + std::to_string(list.items[i].hi) + ",\"why\":\"" + list.items[i].why +
             "\"}";
    }
    return out + ']';
  };
  auto pinList = [](const PinList& list) {
    std::string out = "[";
    for (std::size_t i = 0; i < list.count; ++i) {
      if (i) out += ',';
      out += std::to_string(list.items[i]);
    }
    return out + ']';
  };

  const PinSet& d = soc.defaults;
  return std::string("{\"soc\":\"") + soc.id + "\",\"label\":\"" + soc.label +
         "\",\"max\":" + std::to_string(soc.gpioMax) + ",\"missing\":" + ranges(soc.missing) +
         ",\"inputOnly\":" + ranges(soc.inputOnly) + ",\"reserved\":" + reserved(soc.reserved) +
         ",\"adc1\":" + ranges(soc.adc1) + ",\"strapping\":" + ranges(soc.strapping) +
         ",\"rtc\":" + ranges(soc.rtc) +
         ",\"matrix\":" + pinList(soc.matrix) + ",\"defaults\":{\"pinMatrix\":" +
         std::to_string(d.matrix) + ",\"pinBtnLeft\":" + std::to_string(d.btnLeft) +
         ",\"pinBtnSelect\":" + std::to_string(d.btnSelect) + ",\"pinBtnRight\":" +
         std::to_string(d.btnRight) + ",\"pinBattery\":" + std::to_string(d.battery) +
         ",\"pinLdr\":" + std::to_string(d.ldr) + ",\"pinBuzzer\":" + std::to_string(d.buzzer) +
         ",\"pinI2cSda\":" + std::to_string(d.i2cSda) + ",\"pinI2cScl\":" +
         std::to_string(d.i2cScl) + ",\"pinDfRx\":" + std::to_string(d.dfRx) + ",\"pinDfTx\":" +
         std::to_string(d.dfTx) + ",\"pinI2sBclk\":" + std::to_string(d.i2sBclk) +
         ",\"pinI2sLrclk\":" + std::to_string(d.i2sLrclk) + ",\"pinI2sDout\":" +
         std::to_string(d.i2sDout) + "}}";
}

}
}
