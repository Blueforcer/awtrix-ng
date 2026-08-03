#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace awtrix::script {

enum class ConfigType : uint8_t { Bool, Text, Number, Slider, Select, Color };

constexpr std::size_t kMaxConfigFields = 12;

struct ConfigField {
  std::string key;
  std::string label;
  std::string help;
  std::string unit;
  // Already-encoded JSON, not a display string -- it is spliced straight into the store.
  std::string defJson;
  // Select choices, comma-joined into one string to save the allocations a vector would cost.
  std::string options;
  double min = 0;
  double max = 0;
  double step = 0;
  std::size_t maxLen = 0;
  ConfigType type = ConfigType::Text;
  bool hasMin = false;
  bool hasMax = false;
};

struct ConfigSchema {
  std::vector<ConfigField> fields;
  std::vector<std::string> warnings;
};

ConfigSchema parseConfig(const std::string& source);

bool seedConfigDefaults(const ConfigSchema& schema, const std::string& storeJson,
                        std::string& out);

bool dropUndeclaredValues(const ConfigSchema& before, const ConfigSchema& after,
                          const std::string& storeJson, std::string& out);

void appendConfigJson(std::string& out, const std::string& name, const ConfigSchema& schema,
                      const std::string& storeJson);

struct ConfigPatch {
  bool ok = false;
  std::string message;
  std::string field;
  std::string storeJson;
};

ConfigPatch applyConfigPatch(const ConfigSchema& schema, const std::string& storeJson,
                             const std::string& patchJson);

using ConfigTextFn = std::function<bool(const std::string& name, std::string& out)>;

int configResponse(const std::string& name, const ConfigTextFn& readSource,
                   const ConfigTextFn& readStore, std::string& body);

}
