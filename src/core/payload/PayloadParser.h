#pragma once

#include <functional>
#include <string>
#include <string_view>

#include "core/api/JsonReader.h"

#include "core/Command.h"
#include "core/payload/AppSpec.h"
#include "core/payload/EffectSettingsJson.h"

namespace awtrix {
namespace payload {

enum class JsonParse : uint8_t {
  Ok,
  Malformed,
};

bool readAppSpec(api::JsonReader root, bool isNotification, AppSpec& out,
                 DispatchDetail* err = nullptr);

bool parse(const std::string& json, bool isNotification, AppSpec& out,
           int* arrayElements = nullptr, JsonParse* why = nullptr,
           DispatchDetail* err = nullptr);

inline DispatchResult toDispatchResult(JsonParse p) {
  return p == JsonParse::Malformed ? DispatchResult::ParseError : DispatchResult::Ok;
}

}
}
