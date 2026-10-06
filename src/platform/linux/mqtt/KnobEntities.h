#pragma once

#include "core/mqtt/Entity.h"

namespace awtrix::ha {
inline constexpr Entity kKnobEntities[] = {
    {"btnk",
     R"J("p":"binary_sensor","name":"Knob","ic":"mdi:knob","stat_t":"~/state/buttons/knob","pl_on":"1","pl_off":"0")J",
    },
    {"knob",
     R"J("p":"event","name":"Knob turn","ic":"mdi:knob","stat_t":"~/event/knob","evt_typ":["clockwise","counterclockwise"],"val_tpl":"{\"event_type\":\"{{ 'clockwise' if value_json.turn > 0 else 'counterclockwise' }}\",\"steps\":{{ value_json.turn | abs }}}")J",
    },
};
}
