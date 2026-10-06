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

// The entry point owns this null-terminated table and its contexts for the runtime's lifetime.
struct KeyHandler {
  const char* key;
  void* context;
  bool (*validate)(api::JsonReader root, bool notification, DispatchDetail& error);
  bool (*read)(void* context, api::JsonReader value, AppSpec& spec, DispatchDetail& error);
};
void setKeyHandlers(const KeyHandler* handlers);

enum class JsonParse : uint8_t {
  Ok,
  Malformed,
};

bool readAppSpec(api::JsonReader root, bool isNotification, AppSpec& out,
                 DispatchDetail* err = nullptr);

// Appends the commands of a "draw" array; err names a bad entry as draw[index].
bool readDrawArray(api::JsonReader r, render::DrawProgram& out, DispatchDetail* err);

bool parse(const std::string& json, bool isNotification, AppSpec& out,
           int* arrayElements = nullptr, JsonParse* why = nullptr,
           DispatchDetail* err = nullptr);

inline DispatchResult toDispatchResult(JsonParse p) {
  return p == JsonParse::Malformed ? DispatchResult::ParseError : DispatchResult::Ok;
}

}
}
