#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/net/LinkStatus.h"
#include "core/script/ScriptMeta.h"
#include "core/script/SharedState.h"

namespace awtrix::script {
class ScriptHost;
}

namespace awtrix {
class CoreEngine;
class Canvas;
namespace api {
class JsonWriter;
}

std::string buildSettingsJson(CoreEngine& engine);
std::string buildDisplayJson(CoreEngine& engine);
std::string buildScreenJson(const Canvas& canvas);
std::string buildRadioJson(CoreEngine& engine);
std::string buildAppsJson(CoreEngine& engine, const script::ScriptHost* scripts = nullptr);
std::string buildSharedStateJson(const std::vector<script::SharedEntry>& entries);

void appendRadioJson(std::string& out, CoreEngine& engine);
void appendAppsJson(std::string& out, CoreEngine& engine,
                    const script::ScriptHost* scripts = nullptr,
                    const std::vector<script::StoredScript>* stored = nullptr);
void appendSharedStateJson(std::string& out, const std::vector<script::SharedEntry>& entries);

void writeLinkStatus(api::JsonWriter& w, const net::LinkStatus& status);

// Platform facts the core cannot read for itself; the port fills these in before /device is built.
struct DeviceFacts {
  std::string boardType;
  std::string soc;
  std::string ipAddress;
  std::string hostname;
  int wifiRssi = 0;
  long uptimeSeconds = 0;
  uint32_t freeHeapBytes = 0;
  uint32_t minFreeHeapBytes = 0;
  uint32_t largestFreeBlockBytes = 0;
  uint32_t psramTotalBytes = 0;
  uint32_t psramFreeBytes = 0;
  std::string resetReason;
  bool hasBattery = false;
  bool hasLightSensor = false;
  bool hasTemperature = false;
  bool hasHumidity = false;
  bool hasPressure = false;
  bool scriptingRunning = false;
};

std::string buildDeviceJson(CoreEngine& engine, const std::string& uid, const DeviceFacts& facts);

}
