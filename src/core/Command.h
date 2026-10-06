#pragma once

#include <cstdint>
#include <string>

namespace awtrix {

enum class CommandType : uint8_t {
  None = 0,
  Notify,
  DismissNotify,
  SetPushedApp,
  SetAppOrder,
  SwitchApp,
  NextApp,
  PreviousApp,
  SetSettings,
  SetIndicator,
  Moodlight,
  SetDisplay,
  Sleep,
  // payload is a sound object as sent, arg a sound::PlayAs, name the script whose own call it is.
  PlayAudio,
  // payload is a whole recording, WAV or MP3, which the dispatch takes rather than copies.
  // arg is a sound::Stop, name the script for the script scopes.
  StopAudio,
  Reboot,
  FactoryReset,
  ResetSettings,
  SendScreen,
  ScriptSet,
  ScriptConfigSet,
  ScriptRemove,
  DeleteApp,
  SetRadioStations,
  ScriptUpdate,
  ScriptDataSet,
  BuiltinAppConfigSet,
  SetAppEnabled
};

enum class Source : uint8_t { Mqtt = 0, Http = 1, Menu = 2, Internal = 3 };

struct Command {
  CommandType type = CommandType::None;
  std::string name;
  std::string payload;
  int arg = 0;
  bool clear = false;
  Source source = Source::Internal;

  Command() = default;
  explicit Command(CommandType t) : type(t) {}
};

enum class DispatchResult : uint8_t {
  Ok = 0,
  ParseError,
  ValidationError,
  NotFound,
  Capacity,
  Busy,
  Unavailable,
  Failed,
  Unknown,
  Conflict
};

// Why a command failed, in a shape the API can hand back to the sender. line and hook are only
// filled in for script errors and stay zero/empty everywhere else.
struct DispatchDetail {
  std::string field;
  std::string message;
  int line = 0;
  std::string hook{};

  void clear() {
    field.clear();
    message.clear();
    line = 0;
    hook.clear();
  }
};

}
