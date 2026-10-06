#include "core/mqtt/HaDiscovery.h"

#include <cstring>

#include "core/Transitions.h"

namespace awtrix {
namespace ha {

// Device-level discovery: one retained payload per device declares every entity at once, rather
// than a topic per entity. Publishing an empty payload here retracts the whole device.
std::string discoveryTopic(const DiscoveryContext& ctx) {
  return ctx.haPrefix + "/device/" + ctx.uid + "/config";
}

namespace {

// JSON string escaping that writes in runs, so the usual case of nothing to escape is one write.
// Bytes at or above 0x20 pass through untouched, which leaves UTF-8 intact.
void putEscaped(IByteSink& sink, const std::string& s) {
  std::size_t run = 0;
  for (std::size_t i = 0; i < s.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    const char* esc = nullptr;
    char buf[7];
    switch (c) {
      case '"': esc = "\\\""; break;
      case '\\': esc = "\\\\"; break;
      case '\n': esc = "\\n"; break;
      case '\r': esc = "\\r"; break;
      case '\t': esc = "\\t"; break;
      default:
        if (c < 0x20) {
          static const char* kHex = "0123456789abcdef";
          buf[0] = '\\'; buf[1] = 'u'; buf[2] = '0'; buf[3] = '0';
          buf[4] = kHex[(c >> 4) & 0xF];
          buf[5] = kHex[c & 0xF];
          buf[6] = '\0';
          esc = buf;
        }
        break;
    }
    if (!esc) continue;
    if (i > run) sink.write(s.data() + run, i - run);
    sink.put(esc);
    run = i + 1;
  }
  if (s.size() > run) sink.write(s.data() + run, s.size() - run);
}

void field(IByteSink& sink, const char* key, const std::string& value) {
  sink.put('"');
  sink.put(key);
  sink.put("\":\"");
  putEscaped(sink, value);
  sink.put('"');
}

enum Gate : unsigned char {
  Always = 0,
  Battery = 1 << 0,
  Temperature = 1 << 1,
  Humidity = 1 << 2,
  Pressure = 1 << 3,
  LightSensor = 1 << 4,
  Sound = 1 << 6,
  Radio = 1 << 7,
};

// Volume: body is the entity's name, icon and settings key, each ended by a NUL; the payload
// around them is the same for every volume.
enum Dynamic : unsigned char { NoExtra = 0, TransitionOptions = 1, Volume = 2 };

struct Component {
  const char* key;
  const char* body;
  unsigned char gate;
  unsigned char dyn;
};

// One entry per Home Assistant entity. body is the entity's discovery payload verbatim, in HA's
// abbreviated keys (cmd_t, stat_t, val_tpl, ...); emitComponents adds the "~" base and uniq_id.
constexpr Component kComponents[] = {
    {"mat",
     R"J("p":"light","name":"Matrix","ic":"mdi:clock-digital","cmd_t":"~/cmd/display","pl_on":"{\"power\":true}","pl_off":"{\"power\":false}","stat_t":"~/state/device","stat_val_tpl":"{% if value_json.matrixPower %}{\"power\":true}{% else %}{\"power\":false}{% endif %}","bri_cmd_t":"~/cmd/settings","bri_cmd_tpl":"{\"brightness\":{{ value }}}","bri_stat_t":"~/state/device","bri_val_tpl":"{{ value_json.brightness }}","rgb_cmd_t":"~/cmd/settings","rgb_cmd_tpl":"{\"textColor\":[{{ red }},{{ green }},{{ blue }}]}","rgb_stat_t":"~/state/settings","rgb_val_tpl":"{{ value_json.textColor[1:3]|int(base=16) }},{{ value_json.textColor[3:5]|int(base=16) }},{{ value_json.textColor[5:7]|int(base=16) }}")J",
     Always, NoExtra},

    {"ind1",
     R"J("p":"light","name":"Indicator 1","ic":"mdi:arrow-top-right-thick","on_cmd_type":"first","cmd_t":"~/cmd/indicators/1","pl_on":"{\"color\":[255,255,255]}","pl_off":"{\"color\":[0,0,0]}","stat_t":"~/state/device","stat_val_tpl":"{% if value_json.indicators[0].on %}{\"color\":[255,255,255]}{% else %}{\"color\":[0,0,0]}{% endif %}","rgb_cmd_t":"~/cmd/indicators/1","rgb_cmd_tpl":"{\"color\":[{{ red }},{{ green }},{{ blue }}]}","rgb_stat_t":"~/state/device","rgb_val_tpl":"{{ value_json.indicators[0].color[1:3]|int(base=16) }},{{ value_json.indicators[0].color[3:5]|int(base=16) }},{{ value_json.indicators[0].color[5:7]|int(base=16) }}")J",
     Always, NoExtra},
    {"ind2",
     R"J("p":"light","name":"Indicator 2","ic":"mdi:arrow-right-thick","on_cmd_type":"first","cmd_t":"~/cmd/indicators/2","pl_on":"{\"color\":[255,255,255]}","pl_off":"{\"color\":[0,0,0]}","stat_t":"~/state/device","stat_val_tpl":"{% if value_json.indicators[1].on %}{\"color\":[255,255,255]}{% else %}{\"color\":[0,0,0]}{% endif %}","rgb_cmd_t":"~/cmd/indicators/2","rgb_cmd_tpl":"{\"color\":[{{ red }},{{ green }},{{ blue }}]}","rgb_stat_t":"~/state/device","rgb_val_tpl":"{{ value_json.indicators[1].color[1:3]|int(base=16) }},{{ value_json.indicators[1].color[3:5]|int(base=16) }},{{ value_json.indicators[1].color[5:7]|int(base=16) }}")J",
     Always, NoExtra},
    {"ind3",
     R"J("p":"light","name":"Indicator 3","ic":"mdi:arrow-bottom-right-thick","on_cmd_type":"first","cmd_t":"~/cmd/indicators/3","pl_on":"{\"color\":[255,255,255]}","pl_off":"{\"color\":[0,0,0]}","stat_t":"~/state/device","stat_val_tpl":"{% if value_json.indicators[2].on %}{\"color\":[255,255,255]}{% else %}{\"color\":[0,0,0]}{% endif %}","rgb_cmd_t":"~/cmd/indicators/3","rgb_cmd_tpl":"{\"color\":[{{ red }},{{ green }},{{ blue }}]}","rgb_stat_t":"~/state/device","rgb_val_tpl":"{{ value_json.indicators[2].color[1:3]|int(base=16) }},{{ value_json.indicators[2].color[3:5]|int(base=16) }},{{ value_json.indicators[2].color[5:7]|int(base=16) }}")J",
     Always, NoExtra},

    {"brimode",
     R"J("p":"select","name":"Brightness mode","ic":"mdi:brightness-auto","ops":["Manual","Auto"],"cmd_t":"~/cmd/settings","cmd_tpl":"{\"autoBrightness\":{{ 'true' if value == 'Auto' else 'false' }}}","stat_t":"~/state/settings","val_tpl":"{{ 'Auto' if value_json.autoBrightness else 'Manual' }}")J",
     LightSensor, NoExtra},
    {"transeff",
     R"J("p":"select","name":"Transition effect","ic":"mdi:auto-fix","cmd_t":"~/cmd/settings","cmd_tpl":"{\"transitionEffect\":\"{{ value }}\"}","stat_t":"~/state/settings","val_tpl":"{{ value_json.transitionEffect }}")J",
     Always, TransitionOptions},
    {"trans",
     R"J("p":"switch","name":"Transition","ic":"mdi:swap-horizontal","cmd_t":"~/cmd/settings","pl_on":"{\"autoTransition\":true}","pl_off":"{\"autoTransition\":false}","stat_t":"~/state/settings","val_tpl":"{{ 'ON' if value_json.autoTransition else 'OFF' }}","stat_on":"ON","stat_off":"OFF")J",
     Always, NoExtra},

    {"vol", "Volume\0volume-high\0volume", Sound, Volume},
    {"radvol", "Radio volume\0radio\0radioVolume", Sound | Radio, Volume},
    {"appvol", "App volume\0gamepad-variant\0appVolume", Sound, Volume},
    {"alrtvol", "Alert volume\0bell-ring\0alertVolume", Sound, Volume},
    {"stopsnd",
     R"J("p":"button","name":"Stop sound","ic":"mdi:stop","cmd_t":"~/cmd/audio/stop","pl_prs":"{}")J",
     Sound, NoExtra},

    {"next",
     R"J("p":"button","name":"Next app","ic":"mdi:arrow-right-bold","cmd_t":"~/cmd/apps/next","pl_prs":"{}")J",
     Always, NoExtra},
    {"prev",
     R"J("p":"button","name":"Previous app","ic":"mdi:arrow-left-bold","cmd_t":"~/cmd/apps/previous","pl_prs":"{}")J",
     Always, NoExtra},
    {"dismiss",
     R"J("p":"button","name":"Dismiss notification","ic":"mdi:bell-off","cmd_t":"~/cmd/notify/dismiss","pl_prs":"{}")J",
     Always, NoExtra},

    {"app",
     R"J("p":"sensor","name":"Current app","ic":"mdi:apps","stat_t":"~/state/apps/active")J",
     Always, NoExtra},
    {"ver",
     R"J("p":"sensor","name":"Version","ic":"mdi:tag","ent_cat":"diagnostic","stat_t":"~/state/device","val_tpl":"{{ value_json.version }}")J",
     Always, NoExtra},
    {"ip",
     R"J("p":"sensor","name":"IP address","ic":"mdi:wifi","ent_cat":"diagnostic","stat_t":"~/state/device","val_tpl":"{{ value_json.ipAddress }}")J",
     Always, NoExtra},
    {"prefix",
     R"J("p":"sensor","name":"MQTT prefix","ic":"mdi:tag-text","ent_cat":"diagnostic","stat_t":"~/state/prefix")J",
     Always, NoExtra},
    {"rssi",
     R"J("p":"sensor","name":"WiFi strength","dev_cla":"signal_strength","unit_of_meas":"dBm","ent_cat":"diagnostic","stat_t":"~/state/device","val_tpl":"{{ value_json.wifiRssi }}")J",
     Always, NoExtra},
    {"uptime",
     R"J("p":"sensor","name":"Uptime","dev_cla":"duration","unit_of_meas":"s","ent_cat":"diagnostic","stat_t":"~/state/device","val_tpl":"{{ value_json.uptimeSeconds }}")J",
     Always, NoExtra},
    {"ram",
     R"J("p":"sensor","name":"Free RAM","dev_cla":"data_size","unit_of_meas":"B","ic":"mdi:memory","ent_cat":"diagnostic","stat_t":"~/state/device","val_tpl":"{{ value_json.freeHeapBytes }}")J",
     Always, NoExtra},
    {"light",
     R"J("p":"sensor","name":"Light level","unit_of_meas":"%","ic":"mdi:brightness-percent","stat_t":"~/state/device","val_tpl":"{{ value_json.lightLevel }}")J",
     LightSensor, NoExtra},

    {"temp",
     R"J("p":"sensor","name":"Temperature","dev_cla":"temperature","unit_of_meas":"°C","stat_t":"~/state/device","val_tpl":"{{ value_json.temperature }}")J",
     Temperature, NoExtra},
    {"hum",
     R"J("p":"sensor","name":"Humidity","dev_cla":"humidity","unit_of_meas":"%","stat_t":"~/state/device","val_tpl":"{{ value_json.humidity }}")J",
     Humidity, NoExtra},
    {"press",
     R"J("p":"sensor","name":"Pressure","dev_cla":"pressure","unit_of_meas":"hPa","stat_t":"~/state/device","val_tpl":"{{ value_json.pressureHpa }}")J",
     Pressure, NoExtra},
    {"bat",
     R"J("p":"sensor","name":"Battery","dev_cla":"battery","unit_of_meas":"%","stat_t":"~/state/device","val_tpl":"{{ value_json.batteryPercent }}")J",
     Battery, NoExtra},
    {"batv",
     R"J("p":"sensor","name":"Battery voltage","dev_cla":"voltage","unit_of_meas":"V","ent_cat":"diagnostic","stat_t":"~/state/device","val_tpl":"{{ value_json.batteryVoltage }}")J",
     Battery, NoExtra},
    {"lowbat",
     R"J("p":"binary_sensor","name":"Low battery","dev_cla":"battery","stat_t":"~/state/device","val_tpl":"{{ 'ON' if value_json.lowBattery else 'OFF' }}")J",
     Battery, NoExtra},

    {"btnl",
     R"J("p":"binary_sensor","name":"Button left","ic":"mdi:gesture-tap-button","stat_t":"~/state/buttons/left","pl_on":"1","pl_off":"0")J",
     Always, NoExtra},
    {"btnm",
     R"J("p":"binary_sensor","name":"Button select","ic":"mdi:gesture-tap-button","stat_t":"~/state/buttons/select","pl_on":"1","pl_off":"0")J",
     Always, NoExtra},
    {"btnr",
     R"J("p":"binary_sensor","name":"Button right","ic":"mdi:gesture-tap-button","stat_t":"~/state/buttons/right","pl_on":"1","pl_off":"0")J",
     Always, NoExtra},

};

bool gated(unsigned char gate, const DiscoveryContext& ctx) {
  if ((gate & Battery) && !ctx.hasBattery) return false;
  if ((gate & Temperature) && !ctx.hasTemperature) return false;
  if ((gate & Humidity) && !ctx.hasHumidity) return false;
  if ((gate & Pressure) && !ctx.hasPressure) return false;
  if ((gate & LightSensor) && !ctx.hasLightSensor) return false;
  if ((gate & Sound) && !ctx.hasSound) return false;
  if ((gate & Radio) && !ctx.hasRadio) return false;
  return true;
}

void emitComponent(const DiscoveryContext& ctx, IByteSink& sink, const char* key, const char* body,
                   unsigned char dyn, bool& first) {
  if (!first) sink.put(',');
  first = false;
  sink.put('"');
  sink.put(key);
  sink.put("\":{");
  // "~" is Home Assistant's base-topic shorthand: every "~/..." topic in the bodies expands
  // against this device's MQTT prefix.
  field(sink, "~", ctx.prefix);
  sink.put(',');
  if (dyn == Volume) {
    const char* icon = body + std::strlen(body) + 1;
    const char* setting = icon + std::strlen(icon) + 1;
    sink.put("\"p\":\"number\",\"name\":\"");
    sink.put(body);
    sink.put("\",\"ic\":\"mdi:");
    sink.put(icon);
    sink.put("\",\"min\":0,\"max\":100,\"step\":5,\"unit_of_meas\":\"%\",\"cmd_t\":\"~/cmd/settings\","
             "\"cmd_tpl\":\"{\\\"");
    sink.put(setting);
    sink.put("\\\":{{ value | int }}}\",\"stat_t\":\"~/state/settings\",\"val_tpl\":\"{{ value_json.");
    sink.put(setting);
    sink.put(" }}\"");
  } else {
    sink.put(body);
  }
  if (dyn == TransitionOptions) {
    sink.put(",\"ops\":[");
    for (std::size_t i = 0; i < kTransitionCount; ++i) {
      if (i) sink.put(',');
      sink.put('"');
      sink.put(kTransitionNames[i]);
      sink.put('"');
    }
    sink.put(']');
  }
  sink.put(",\"uniq_id\":\"");
  putEscaped(sink, ctx.uid);
  sink.put('_');
  sink.put(key);
  sink.put("\"}");
}

void emitComponents(const DiscoveryContext& ctx, IByteSink& sink) {
  sink.put(",\"cmps\":{");
  bool first = true;
  for (const Component& c : kComponents)
    if (gated(c.gate, ctx)) emitComponent(ctx, sink, c.key, c.body, c.dyn, first);
  ctx.eachPlatformEntity([&](const Entity& entity) {
    emitComponent(ctx, sink, entity.key, entity.body, NoExtra, first);
  });
  sink.put('}');
}

}

// Streams the whole discovery document; nothing is buffered, because it is well past the MQTT
// client's write buffer. Must be deterministic: the caller emits it twice, first only to count.
void emit(const DiscoveryContext& ctx, IByteSink& sink) {
  sink.put('{');
  field(sink, "avty_t", ctx.prefix + "/availability");
  sink.put(",\"pl_avail\":\"online\","
           "\"pl_not_avail\":\"offline\",\"dev\":{");
  field(sink, "ids", ctx.uid);
  sink.put(',');
  field(sink, "name", ctx.hostname);
  sink.put(',');
  field(sink, "sw", ctx.version);
  if (!ctx.url.empty()) {
    sink.put(',');
    field(sink, "cu", ctx.url);
  }
  sink.put(",\"mf\":\"Blueforcer\",\"mdl\":\"AWTRIX NG\"},\"o\":{\"name\":\"awtrix-ng\",");
  field(sink, "sw", ctx.version);
  sink.put('}');
  emitComponents(ctx, sink);
  sink.put('}');
}

}
}
