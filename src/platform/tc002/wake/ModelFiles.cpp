#include "platform/tc002/wake/ModelFiles.h"

#include <algorithm>
#include <limits>

#include "core/api/JsonReader.h"
#include "platform/posix/Files.h"
#include "platform/tc002/wake/WakeFrontend.h"

namespace awtrix::tc002::wake {
namespace {
constexpr std::size_t kModelLimit = 512 * 1024, kSettingsLimit = 4096;
constexpr long long kFrontendStepMs = WakeFrontend::kStepMs;

std::size_t clampedSize(long long value) {
  if (value < 0) return 0;
  return static_cast<std::size_t>(
      std::min<unsigned long long>(static_cast<unsigned long long>(value), std::numeric_limits<std::size_t>::max()));
}

bool settings(const api::JsonReader& object, double& cutoff, std::size_t& window) {
  long long size = 0;
  if (!api::memberValue(object, "probability_cutoff").asDouble(cutoff) ||
      !api::memberValue(object, "sliding_window_size").asLong(size))
    return false;
  window = clampedSize(size);
  return true;
}
}

bool readModelFiles(const std::string& modelPath, ModelFiles& out, std::string& error) {
  const auto dot = modelPath.rfind('.');
  out.settingsPath = (dot == std::string::npos ? modelPath : modelPath.substr(0, dot)) + ".json";
  std::string json;
  if (!posix::readText(out.settingsPath, json, kSettingsLimit)) {
    error = "cannot read " + out.settingsPath;
    return false;
  }
  const api::JsonReader root(json);
  const api::JsonReader micro = api::memberValue(root, "micro");
  const bool top = root.isObject() && settings(root, out.cutoff, out.window);
  if (!top && !(root.isObject() && settings(micro, out.cutoff, out.window))) {
    error = out.settingsPath + " needs probability_cutoff and sliding_window_size";
    return false;
  }
  const api::JsonReader step = api::memberValue(top ? root : micro, "feature_step_size");
  long long stepMs = 0;
  if (api::present(step) && (!step.asLong(stepMs) || stepMs != kFrontendStepMs)) {
    error = out.settingsPath + ": feature_step_size must be " + std::to_string(kFrontendStepMs);
    return false;
  }
  if (!posix::readText(modelPath, out.model, kModelLimit)) {
    error = "cannot read " + modelPath;
    return false;
  }
  return true;
}

}
