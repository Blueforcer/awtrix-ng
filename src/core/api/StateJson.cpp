#include "core/api/StateJson.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

#include "AppConfig.h"
#include "core/CoreEngine.h"
#include "core/DeviceCapabilities.h"
#include "core/api/JsonText.h"
#include "core/api/JsonWriter.h"
#include "core/apps/BuiltinAppConfig.h"
#include "core/render/Canvas.h"
#include "core/render/Color.h"
#include "core/script/ScriptHeap.h"
#include "core/script/ScriptHost.h"

namespace awtrix {

namespace {
double roundTo(float v, int decimals) {
  double f = 1.0;
  for (int i = 0; i < decimals; ++i) f *= 10.0;
  return std::round(static_cast<double>(v) * f) / f;
}

// Upper-case and colon-separated, as routers list the devices they admit.
std::string macText(const std::array<uint8_t, 6>& mac) {
  char text[18];
  std::snprintf(text, sizeof(text), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3],
                mac[4], mac[5]);
  return text;
}

void writeMirrorStatus(api::JsonWriter& w, const mirror::Status& status) {
  w.beginObject();
  w.member("sharing", status.sharing);
  w.member("viewers", static_cast<unsigned>(status.viewers));
  w.member("source", status.source);
  w.member("state", mirror::followStateName(status.follow));
  if (status.sourceWidth > 0) {
    w.member("sourceWidth", static_cast<unsigned>(status.sourceWidth));
    w.member("sourceHeight", static_cast<unsigned>(status.sourceHeight));
  }
  w.endObject();
}
}

std::string buildDeviceJson(CoreEngine& engine, const std::string& uid, const DeviceFacts& facts,
                            const DeviceMembersFn& platformMembers) {
  const RuntimeState& rt = engine.state().runtime();
  std::string out;
  out.reserve(1024);
  api::JsonWriter w(out);
  w.beginObject();
  w.member("version", AWTRIX_NG_VERSION);
  w.member("uid", uid);
  w.member("boardType", facts.boardType);
  w.member("soc", facts.soc);
  w.member("updateImage", facts.updateImage);
  w.member("ipAddress", facts.ipAddress);
  if (facts.hasMacAddress) w.member("macAddress", macText(facts.macAddress));
  w.member("hostname", facts.hostname);
  w.member("wifiRssi", facts.wifiRssi);
  w.member("uptimeSeconds", facts.uptimeSeconds);
  w.member("freeHeapBytes", facts.freeHeapBytes);
  w.member("minFreeHeapBytes", facts.minFreeHeapBytes);
  if (facts.hasLargestFreeBlock) w.member("largestFreeBlockBytes", facts.largestFreeBlockBytes);
  if (facts.psramTotalBytes) {
    w.member("psramTotalBytes", facts.psramTotalBytes);
    w.member("psramFreeBytes", facts.psramFreeBytes);
  }
  w.member("scriptingRunning", facts.scriptingRunning);
  const script::heap::Info scriptHeap = script::heap::info();
  w.member("scriptHeapPool", scriptHeap.name);
  w.member("scriptHeapBudgetBytes", scriptHeap.budgetBytes);
  w.member("resetReason", facts.resetReason);
  w.member("fps", rt.fps);
  w.member("brightness", rt.brightnessActual);
  if (facts.hasLightSensor) {
    w.member("lightLevel", roundTo(rt.lightLevel, 1), 1);
    w.member("ldrRaw", rt.ldrRaw);
  }
  if (facts.hasBattery) {
    w.member("batteryPercent", rt.batteryPercent);
    w.member("batteryVoltage", roundTo(rt.batteryVoltage, 2), 2);
    if (facts.hasBatteryDivider) w.member("batteryPinMillivolts", rt.batteryPinMillivolts);
    w.member("lowBattery", rt.lowBattery);
  }
  if (facts.hasTemperature) w.member("temperature", roundTo(rt.temperatureC, 1), 1);
  if (facts.hasHumidity) w.member("humidity", roundTo(rt.humidity, 1), 1);
  if (facts.hasPressure) w.member("pressureHpa", roundTo(rt.pressureHpa, 1), 1);
  w.member("matrixPower", !rt.matrixOff);
  w.member("currentApp", engine.currentAppId());
  w.key("indicators");
  w.beginArray();
  for (const Indicator& ind : rt.indicators) {
    w.beginObject();
    w.member("on", ind.on);
    w.member("color", color::toHex(ind.color));
    w.member("blinkMs", ind.blinkMs);
    w.member("fadeMs", ind.fadeMs);
    w.endObject();
  }
  w.endArray();
  w.member("messageCount", rt.receivedMessages);
  w.key("wifi");
  writeLinkStatus(w, rt.wifi);
  w.key("mqtt");
  writeLinkStatus(w, rt.mqtt);
  w.key("mirror");
  writeMirrorStatus(w, rt.mirror);
  if (platformMembers) platformMembers(w);
  w.endObject();
  return out;
}

void writeLinkStatus(api::JsonWriter& w, const net::LinkStatus& status) {
  w.beginObject();
  w.member("enabled", status.enabled);
  w.member("state", net::linkPhaseName(status.phase));
  w.member("host", status.host);
  w.member("endpoint", status.endpoint);
  w.member("attempts", static_cast<unsigned>(status.attempts));
  w.member("retryInMs", static_cast<unsigned long>(status.retryInMs));
  w.member("connects", static_cast<unsigned long>(status.connects));
  const char* error = net::linkErrorName(status.error);
  if (*error) w.member("error", error);
  else w.memberNull("error");
  const char* last = net::linkErrorName(status.lastError);
  if (*last) w.member("lastError", last);
  else w.memberNull("lastError");
  w.endObject();
}

std::string buildSettingsJson(CoreEngine& engine) {
  std::string out;
  out.reserve(1536);
  api::JsonWriter w(out);
  w.beginObject();
  engine.state().settings().writeMembers(w);
  w.endObject();
  return out;
}

std::string buildDisplayJson(CoreEngine& engine) {
  const RuntimeState& rt = engine.state().runtime();
  std::string out;
  out.reserve(512);
  api::JsonWriter w(out);
  w.beginObject();
  w.member("power", !rt.matrixOff);
  w.member("brightness", rt.brightnessActual);
  if (rt.globalOverlay.empty())
    w.memberNull("overlay");
  else
    w.member("overlay", rt.globalOverlay);
  {
    const EffectSettings& os = rt.globalOverlaySettings;
    w.key("overlaySettings");
    w.beginObject();
    w.member("speed", os.hasSpeed ? os.speed : 1.0f);
    if (os.ramp.valid()) {
      w.key("palette");
      w.beginArray();
      for (uint32_t e : os.ramp.palette().entries) w.value(color::toHex(e));
      w.endArray();
    } else {
      w.memberNull("palette");
    }
    w.member("blend", os.ramp.blend);
    w.endObject();
  }
  if (rt.moodlightMode) {
    w.key("moodlight");
    w.beginObject();
    w.member("color", color::toHex(rt.moodlightColor));
    w.member("brightness", rt.moodlightBrightness);
    w.endObject();
  } else {
    w.memberNull("moodlight");
  }
  w.endObject();
  return out;
}

// Measures the exact output length first: a whole matrix of decimal pixel values would otherwise
// grow the string dozens of times. Any alpha byte is masked off.
std::string buildScreenJson(const Canvas& canvas) {
  const std::size_t n = canvas.size();
  std::size_t need = 48;
  for (std::size_t i = 0; i < n; ++i) {
    const unsigned v = static_cast<unsigned>(canvas.data()[i] & 0xFFFFFFu);
    unsigned digits = 1;
    for (unsigned t = v; t >= 10; t /= 10) ++digits;
    need += digits + 1;
  }
  std::string out;
  out.reserve(need);
  out += "{\"width\":";
  out += std::to_string(canvas.width());
  out += ",\"height\":";
  out += std::to_string(canvas.height());
  out += ",\"pixels\":[";
  char buf[12];
  for (std::size_t i = 0; i < n; ++i) {
    if (i) out += ',';
    snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(canvas.data()[i] & 0xFFFFFFu));
    out += buf;
  }
  out += "]}";
  return out;
}

using api::appendInt;
using api::appendJsonString;

void appendSharedStateJson(std::string& out, const std::vector<script::SharedEntry>& entries) {
  out += '[';
  for (std::size_t i = 0; i < entries.size(); ++i) {
    const script::SharedEntry& e = entries[i];
    if (i) out += ',';
    out += "{\"owner\":";
    appendJsonString(out, e.owner);
    out += ",\"key\":";
    appendJsonString(out, e.key);
    out += ",\"type\":\"";
    switch (e.value.type) {
      case script::SharedState::Type::Int: out += "int"; break;
      case script::SharedState::Type::Real: out += "real"; break;
      case script::SharedState::Type::Bool: out += "bool"; break;
      case script::SharedState::Type::Str: out += "string"; break;
    }
    out += "\",\"value\":";
    switch (e.value.type) {
      case script::SharedState::Type::Int:
        appendInt(out, e.value.i);
        break;
      case script::SharedState::Type::Real: {
        if (e.value.r != e.value.r || e.value.r > 1e308 || e.value.r < -1e308) {
          out += "null";
        } else {
          char buf[32];
          snprintf(buf, sizeof(buf), "%.6g", e.value.r);
          out += buf;
        }
        break;
      }
      case script::SharedState::Type::Bool:
        out += e.value.i != 0 ? "true" : "false";
        break;
      case script::SharedState::Type::Str:
        appendJsonString(out, e.value.s);
        break;
    }
    out += ",\"ageMs\":";
    appendInt(out, e.ageMs);
    out += '}';
  }
  out += ']';
}

static void appendIconsJson(std::string& out, const std::string& raw) {
  out += ",\"icons\":[";
  bool first = true;
  for (const std::string& id : script::splitIcons(raw)) {
    if (!first) out += ',';
    first = false;
    appendJsonString(out, id);
  }
  out += ']';
}

// A requirement is met by an installed script of that name or a module imported under it.
static bool hasRequirement(const std::string& name,
                           const std::map<std::string, script::ScriptHost::Info>& info,
                           const std::vector<script::StoredScript>* stored) {
  const auto app = info.find(name);
  if (app != info.end() && !app->second.module) return true;
  for (const auto& kv : info)
    if (kv.second.module && kv.second.importName == name) return true;
  if (stored)
    for (const auto& script : *stored)
      if ((script.meta.module && !script.meta.moduleName.empty() ? script.meta.moduleName : script.name) == name)
        return true;
  return false;
}

static void appendRequiresJson(std::string& out, const std::string& raw,
                               const std::map<std::string, script::ScriptHost::Info>& info,
                               const std::vector<script::StoredScript>* stored) {
  out += ",\"requires\":[";
  bool first = true;
  for (const script::Requirement& r : script::splitRequires(raw)) {
    if (!first) out += ',';
    first = false;
    out += "{\"name\":";
    appendJsonString(out, r.name);
    if (!r.hub.empty()) {
      out += ",\"hub\":";
      appendJsonString(out, r.hub);
    }
    out += ",\"missing\":";
    out += hasRequirement(r.name, info, stored) ? "false" : "true";
    out += '}';
  }
  out += ']';
}

// What a script asks of the device (@needs, @display) and, where the device is known, whether it
// has it. The device never refuses a script over this; it only reports.
static void appendCompatibilityJson(std::string& out, const std::string& needs, int width, int height,
                                    const DeviceCapabilities* device) {
  out += ",\"needs\":[";
  bool first = true;
  for (const std::string& name : script::splitNeeds(needs)) {
    if (!first) out += ',';
    first = false;
    out += "{\"name\":";
    appendJsonString(out, name);
    if (device) {
      out += ",\"missing\":";
      out += device->has(name) ? "false" : "true";
    }
    out += '}';
  }
  out += "],\"display\":";
  if (width <= 0 || height <= 0) {
    out += "null";
    return;
  }
  out += "{\"width\":" + std::to_string(width) + ",\"height\":" + std::to_string(height);
  if (device) {
    out += ",\"fits\":";
    out += device->fits(width, height) ? "true" : "false";
  }
  out += '}';
}

// Three passes: apps in the rotation report their slot index, apps that exist but sit outside it
// report slot null, and script modules are listed last since they never occupy a slot.

void appendAppsJson(std::string& out, CoreEngine& engine, const script::ScriptHost* scripts,
                    const std::vector<script::StoredScript>* stored, const DeviceCapabilities* device) {
  const std::map<std::string, script::ScriptHost::Info> info =
      scripts ? scripts->list() : std::map<std::string, script::ScriptHost::Info>{};

  const std::vector<std::string> here = engine.knownApps();

  const auto* offlineScripts = scripts ? nullptr : stored;

  out += '[';
  bool first = true;
  auto add = [&](const std::string& id, int slot) {
    const bool inLoop = engine.isInLoop(id);
    if (!first) out += ',';
    first = false;
    out += "{\"name\":";
    appendJsonString(out, id);
    out += ",\"enabled\":";
    out += engine.isEnabled(id) ? "true" : "false";
    out += ",\"inLoop\":";
    out += inLoop ? "true" : "false";
    out += ",\"slot\":";
    out += slot < 0 ? "null" : std::to_string(slot);
    const bool present =
        std::find(here.begin(), here.end(), id) != here.end() || engine.isOnDemandApp(id);
    out += ",\"present\":";
    out += present ? "true" : "false";

    const AppSpec* spec = engine.pushedApp(id);
    const auto it = info.find(id);
    if (spec) {
      out += ",\"origin\":\"pushed\"";
      if (!spec->icon.empty()) {
        out += ",\"icon\":";
        appendJsonString(out, spec->icon);
      }
    } else if (engine.isScriptApp(id)) {
      out += ",\"origin\":\"script\"";
      if (it != info.end()) {
        out += ",\"skipped\":";
        out += it->second.skipping ? "true" : "false";
        out += ",\"headless\":";
        out += it->second.headless ? "true" : "false";
        out += ",\"ondemand\":";
        out += it->second.onDemand ? "true" : "false";
        out += ",\"config\":";
        out += it->second.config ? "true" : "false";
        const script::ScriptError& e = it->second.error;
        if (e.empty()) {
          out += ",\"error\":null";
        } else {
          out += ",\"error\":{\"message\":";
          appendJsonString(out, e.message);
          if (e.line > 0) out += ",\"line\":" + std::to_string(e.line);
          if (!e.hook.empty()) {
            out += ",\"hook\":";
            appendJsonString(out, e.hook);
          }
          out += '}';
        }
        out += ",\"meta\":{\"name\":";
        appendJsonString(out, it->second.metaName);
        out += ",\"desc\":";
        appendJsonString(out, it->second.desc);
        out += ",\"author\":";
        appendJsonString(out, it->second.author);
        out += ",\"version\":";
        appendJsonString(out, it->second.version);
        appendIconsJson(out, it->second.icons);
        appendRequiresJson(out, it->second.requirements, info, offlineScripts);
        appendCompatibilityJson(out, it->second.needs, it->second.displayWidth, it->second.displayHeight, device);
        out += '}';
      }
    } else if (present) {
      out += ",\"origin\":\"builtin\"";
      out += ",\"config\":";
      out += builtinconfig::hasApp(id) ? "true" : "false";
    } else {
      out += ",\"origin\":null";
    }
    out += '}';
  };
  auto addModule = [&](const std::string& id, const script::ScriptHost::Info& mod) {
    if (!first) out += ',';
    first = false;
    out += "{\"name\":";
    appendJsonString(out, id);
    out += ",\"origin\":\"module\",\"import\":";
    appendJsonString(out, mod.importName);
    out += ",\"config\":";
    out += mod.config ? "true" : "false";
    if (mod.error.empty()) {
      out += ",\"error\":null";
    } else {
      out += ",\"error\":{\"message\":";
      appendJsonString(out, mod.error.message);
      if (mod.error.line > 0) out += ",\"line\":" + std::to_string(mod.error.line);
      out += '}';
    }
    out += ",\"meta\":{\"name\":";
    appendJsonString(out, mod.metaName);
    out += ",\"desc\":";
    appendJsonString(out, mod.desc);
    out += ",\"author\":";
    appendJsonString(out, mod.author);
    out += ",\"version\":";
    appendJsonString(out, mod.version);
    appendIconsJson(out, mod.icons);
    appendRequiresJson(out, mod.requirements, info, offlineScripts);
    appendCompatibilityJson(out, mod.needs, mod.displayWidth, mod.displayHeight, device);
    out += "}}";
  };

  int i = 0;
  for (const auto& id : engine.appOrder()) add(id, i++);
  for (const auto& id : engine.allApps())
    if (engine.slotOf(id) < 0) add(id, -1);
  for (const auto& id : engine.onDemandApps()) add(id, -1);
  for (const auto& kv : info)
    if (kv.second.module) addModule(kv.first, kv.second);
  // With no script host running, fall back to what is on flash so the UI still lists the
  // scripts that would load.
  if (!scripts && stored) {
    for (const script::StoredScript& s : *stored) {
      if (engine.isScriptApp(s.name)) continue;
      if (s.meta.module) {
        script::ScriptHost::Info mod;
        mod.module = true;
        mod.importName = s.meta.moduleName.empty() ? s.name : s.meta.moduleName;
        mod.metaName = s.meta.name;
        mod.desc = s.meta.desc;
        mod.author = s.meta.author;
        mod.version = s.meta.version;
        mod.icons = s.meta.icons;
        mod.requirements = s.meta.requirements;
        mod.needs = s.meta.needs;
        mod.displayWidth = s.meta.displayWidth;
        mod.displayHeight = s.meta.displayHeight;
        addModule(s.name, mod);
        continue;
      }
      if (!first) out += ',';
      first = false;
      out += "{\"name\":";
      appendJsonString(out, s.name);
      out += ",\"enabled\":false,\"inLoop\":false,\"slot\":null,\"present\":true,\"origin\":\"script\"";
      out += ",\"skipped\":false";
      out += ",\"headless\":";
      out += s.meta.headless ? "true" : "false";
      out += ",\"ondemand\":";
      out += s.meta.onDemand ? "true" : "false";
      out += ",\"config\":";
      out += s.meta.hasConfig ? "true" : "false";
      out += ",\"error\":null,\"meta\":{\"name\":";
      appendJsonString(out, s.meta.name);
      out += ",\"desc\":";
      appendJsonString(out, s.meta.desc);
      out += ",\"author\":";
      appendJsonString(out, s.meta.author);
      out += ",\"version\":";
      appendJsonString(out, s.meta.version);
      appendIconsJson(out, s.meta.icons);
      appendRequiresJson(out, s.meta.requirements, info, offlineScripts);
      appendCompatibilityJson(out, s.meta.needs, s.meta.displayWidth, s.meta.displayHeight, device);
      out += "}}";
    }
  }
  out += ']';
}

namespace {
void appendGroup(std::string& out, const char* key, const sound::GroupStatus& status) {
  out += ",\"";
  out += key;
  out += "\":{\"playing\":";
  out += status.playing ? "true" : "false";
  out += ",\"name\":";
  appendJsonString(out, status.name);
  out += ",\"error\":";
  appendJsonString(out, status.error);
  out += '}';
}
}

// One document for the whole output: the radio with its health, then what the app and the
// alerts play, then the station list.
void appendAudioJson(std::string& out, CoreEngine& engine) {
  const RuntimeState& runtime = engine.state().runtime();
  out += "{\"radio\":{\"playing\":";
  out += runtime.radioPlaying ? "true" : "false";
  out += ",\"station\":";
  appendJsonString(out, runtime.radioStation);
  out += ",\"title\":";
  appendJsonString(out, runtime.radioTitle);
  out += ",\"error\":";
  appendJsonString(out, runtime.radioError);
  out += ",\"underruns\":" + std::to_string(engine.radioUnderruns());
  out += ",\"decodeUs\":" + std::to_string(engine.radioDecodeUs());
  out += ",\"starvedMs\":" + std::to_string(engine.radioStarvedMs());
  out += ",\"bufferBytes\":" + std::to_string(engine.radioBufferBytes()) + "}";
  appendGroup(out, "app", engine.audio().appStatus());
  appendGroup(out, "alert", engine.audio().alertStatus());
  // stationsJson() hands back a whole document; splice out just the array it contains.
  const std::string stations = engine.stationsJson();
  const std::size_t open = stations.find('[');
  const std::size_t close = stations.rfind(']');
  out += ",\"stations\":";
  out += (open == std::string::npos || close == std::string::npos)
             ? "[]"
             : stations.substr(open, close - open + 1);
  out += '}';
}

std::string buildSharedStateJson(const std::vector<script::SharedEntry>& entries) {
  std::string out;
  appendSharedStateJson(out, entries);
  return out;
}

std::string buildAudioJson(CoreEngine& engine) {
  std::string out;
  appendAudioJson(out, engine);
  return out;
}

}
