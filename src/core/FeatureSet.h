#pragma once

#include <string>
#include "core/Command.h"
#include "core/api/JsonReader.h"
#include "core/sound/AudioRouter.h"

namespace awtrix {
// Build support, separate from the audio sinks actually installed at runtime.
struct FeatureSet {
  bool scripting = true;
  bool mp3 = true;
  bool radio = true;
  bool outboundTls = true;
  bool browserOta = true;
};

inline DispatchResult featurePolicy(const FeatureSet& features, const std::string& path) {
  struct Rule { const char* path; bool prefix; bool FeatureSet::*enabled; };
  static const Rule rules[] = {
      {"/api/v1/scripts/", true, &FeatureSet::scripting},
      {"/api/v1/apps/script/", true, &FeatureSet::scripting},
      {"/api/v1/apps/script-update/", true, &FeatureSet::scripting},
      {"/api/v1/audio/mp3", false, &FeatureSet::mp3},
      {"/api/v1/audio/mp3/", true, &FeatureSet::mp3},
      {"/api/v1/audio/stations", false, &FeatureSet::radio},
      {"cmd/audio/stations", false, &FeatureSet::radio},
      {"/update", false, &FeatureSet::browserOta},
  };
  for (const auto& rule : rules)
    if (!(features.*rule.enabled) &&
        (rule.prefix ? path.rfind(rule.path, 0) == 0 : path == rule.path))
      return DispatchResult::Unavailable;
  if (!features.scripting && path.rfind("/api/v1/apps/", 0) == 0 &&
      path.size() >= 7 && path.compare(path.size() - 7, 7, "/config") == 0)
    return DispatchResult::Unavailable;
  return DispatchResult::Ok;
}

inline DispatchResult featurePolicy(const FeatureSet& features, const Command& cmd) {
  struct Rule { CommandType command; bool FeatureSet::*enabled; };
  static const Rule rules[] = {
      {CommandType::ScriptSet, &FeatureSet::scripting},
      {CommandType::ScriptUpdate, &FeatureSet::scripting},
      {CommandType::ScriptConfigSet, &FeatureSet::scripting},
      {CommandType::ScriptRemove, &FeatureSet::scripting},
      {CommandType::PlayStream, &FeatureSet::radio},
      {CommandType::SetRadioStations, &FeatureSet::radio},
  };
  for (const auto& rule : rules)
    if (cmd.type == rule.command && !(features.*rule.enabled))
      return DispatchResult::Unavailable;
  if (cmd.type == CommandType::PlayAudio &&
      cmd.arg == static_cast<int>(sound::Source::Mp3) && !features.mp3)
    return DispatchResult::Unavailable;
  if (cmd.type == CommandType::PlayStream && !features.outboundTls) {
    api::JsonReader url;
    std::string value;
    if (api::readMembers(cmd.payload, {{"url", &url}}) && url.appendString(value) &&
        value.rfind("https://", 0) == 0) return DispatchResult::Unavailable;
  }
  return DispatchResult::Ok;
}
}
