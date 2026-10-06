#include "core/Dispatcher.h"

#include <cctype>
#include <climits>
#include <string_view>
#include <utility>

#include "core/JsonColor.h"
#include "core/StateStore.h"
#include "core/api/JsonCoerce.h"
#include "core/api/JsonReader.h"
#include "core/effects/EffectRegistry.h"
#include "core/payload/PayloadParser.h"
#include "core/render/Color.h"
#include "core/sound/SoundMp3.h"
#include "core/sound/SoundSpec.h"

namespace awtrix {

namespace {

std::string toLower(const char* s) {
  std::string out = s ? s : "";
  for (char& ch : out) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return out;
}

DispatchResult applySettings(const std::string& payload, CommandContext& ctx) {
  if (!api::isWellFormed(payload)) return DispatchResult::ParseError;
  SettingsError err;
  if (!Settings::validateRead(api::JsonReader(payload), err)) {
    ctx.detail = {err.field, err.message};
    return DispatchResult::ValidationError;
  }
  Settings& settings = ctx.state.settings();
  settings.applyRead(api::JsonReader(payload));
  // weekdayBar sets the Date app's bar too; dateWeekdayBar wins wherever it stands.
  for (const char* key : {"weekdayBar", "dateWeekdayBar"}) {
    api::JsonReader reader(payload);
    if (!reader.enterObject()) continue;
    while (reader.nextMember()) {
      if (reader.keyEquals(key)) {
        weekdaybar::Error ignored;
        weekdaybar::read(reader, settings.dateWeekdayBar, ignored);
      }
      if (!reader.skipValue()) break;
    }
  }
  ctx.state.emit(StateEvent::SettingsChanged);
  return DispatchResult::Ok;
}

DispatchResult applyDisplay(const std::string& payload, CommandContext& ctx) {
  api::JsonReader atPower, atOverlay, atOverlaySettings;
  if (!api::readMembers(payload, {{"power", &atPower},
                                  {"overlay", &atOverlay},
                                  {"overlaySettings", &atOverlaySettings}}))
    return DispatchResult::ParseError;

  const bool hasPower = api::present(atPower);
  bool power = false;
  if (hasPower && !atPower.asBool(power)) {
    ctx.detail = {"power", "must be a boolean"};
    return DispatchResult::ValidationError;
  }
  const bool hasOverlay = api::present(atOverlay);
  std::string overlay;
  if (hasOverlay && !atOverlay.isNull()) {
    std::string name;
    if (!atOverlay.isString() || !atOverlay.appendString(name)) {
      ctx.detail = {"overlay", "must be a string or null"};
      return DispatchResult::ValidationError;
    }
    overlay = toLower(name.c_str());
    if (ctx.overlays && !overlay.empty() && !ctx.overlays->find(overlay)) {
      ctx.detail = {"overlay", "unknown overlay"};
      return DispatchResult::ValidationError;
    }
  }
  const bool hasOverlaySettings = api::present(atOverlaySettings);
  EffectSettings overlaySettings;
  if (hasOverlaySettings) {
    if (!atOverlaySettings.isObject()) {
      ctx.detail = {"overlaySettings", "must be an object"};
      return DispatchResult::ValidationError;
    }
    if (!payload::readEffectSettings(atOverlaySettings, overlaySettings)) {
      ctx.detail = {"overlaySettings.palette", "unknown palette"};
      return DispatchResult::ValidationError;
    }
  }

  if (hasPower) {
    ctx.state.runtime().matrixOff = !power;
    ctx.state.emit(StateEvent::PowerChanged);
  }
  if (hasOverlay) {
    ctx.state.runtime().globalOverlay = overlay;
    if (overlay.empty()) ctx.state.runtime().globalOverlaySettings = EffectSettings{};
  }
  if (hasOverlaySettings) ctx.state.runtime().globalOverlaySettings = overlaySettings;
  return DispatchResult::Ok;
}

// idx is the 1..3 the API speaks; the indicators are stored 0-based. An empty body or {} clears.
DispatchResult applyIndicator(int idx, const std::string& payload, bool clear, CommandContext& ctx) {
  if (idx < 1 || idx > 3) return DispatchResult::Failed;
  Indicator& ind = ctx.state.runtime().indicators[idx - 1];
  if (clear || api::isEmptyObject(payload)) {
    ind = Indicator{};
    ctx.state.emit(StateEvent::IndicatorChanged);
    return DispatchResult::Ok;
  }
  api::JsonReader atColor, atBlink, atFade;
  if (!api::readMembers(payload,
                        {{"color", &atColor}, {"blinkMs", &atBlink}, {"fadeMs", &atFade}}))
    return DispatchResult::ParseError;

  if (api::present(atColor)) {
    uint32_t col = 0;
    // null and black both mean "off", and both leave the stored colour alone so blinkMs and fadeMs
    // survive until the indicator is switched back on.
    if (atColor.isNull()) {
      ind.on = false;
    } else if (!color::readColor(atColor, col)) {
      ctx.detail = {"color",
                    "must be a color"};
      return DispatchResult::ValidationError;
    } else if (col == 0) {
      ind.on = false;
    } else {
      ind.color = col;
      ind.on = true;
    }
  }
  if (api::present(atBlink)) ind.blinkMs = api::coerceInt<uint16_t>(atBlink);
  if (api::present(atFade)) ind.fadeMs = api::coerceInt<uint16_t>(atFade);
  ctx.state.emit(StateEvent::IndicatorChanged);
  return DispatchResult::Ok;
}

DispatchResult applyMoodlight(const std::string& payload, bool clear, CommandContext& ctx) {
  RuntimeState& rt = ctx.state.runtime();
  if (clear || api::isEmptyObject(payload)) {
    rt.moodlightMode = false;
    ctx.state.emit(StateEvent::MoodlightChanged);
    return DispatchResult::Ok;
  }
  api::JsonReader atKelvin, atColor, atBrightness;
  if (!api::readMembers(payload, {{"kelvin", &atKelvin},
                                  {"color", &atColor},
                                  {"brightness", &atBrightness}}))
    return DispatchResult::ParseError;

  // kelvin wins when both it and color are sent.
  if (api::present(atKelvin)) {
    rt.moodlightColor = color::fromKelvin(api::coerceInt<int>(atKelvin));
  } else if (api::present(atColor)) {
    uint32_t col = 0;
    if (!color::readColor(atColor, col)) {
      ctx.detail = {"color",
                    "must be a color"};
      return DispatchResult::ValidationError;
    }
    rt.moodlightColor = col;
  }
  if (api::present(atBrightness))
    rt.moodlightBrightness = static_cast<uint8_t>(api::coerceInt<int>(atBrightness));
  rt.moodlightMode = true;
  ctx.state.emit(StateEvent::MoodlightChanged);
  return DispatchResult::Ok;
}

DispatchResult applySleep(const std::string& payload, CommandContext& ctx) {
  api::JsonReader atDuration;
  if (!api::readMembers(payload, {{"durationMs", &atDuration}}))
    return DispatchResult::ParseError;

  long long ms = 0;
  if (!atDuration.isNumber() || !atDuration.isInteger() || !atDuration.asLong(ms) || ms <= 0 ||
      ms > LONG_MAX) {
    ctx.detail = {"durationMs", "must be an integer > 0"};
    return DispatchResult::ValidationError;
  }
  ctx.state.runtime().matrixOff = true;
  ctx.state.emit(StateEvent::PowerChanged);
  ctx.system.sleep(static_cast<uint64_t>(ms));
  return DispatchResult::Ok;
}

// A station is a name from the list, a position in it, or a stream address.
DispatchResult applyRadioPlay(const sound::Spec& spec, CommandContext& ctx) {
  if (!ctx.audio.caps().radio || !ctx.stations) {
    ctx.detail = {"", "no audio output"};
    return DispatchResult::Unavailable;
  }
  std::string url;
  std::string label;
  if (spec.text.empty()) {
    label = ctx.stations->stationNameAt(spec.number);
    if (label.empty()) {
      ctx.detail = {"station", "no station at that position"};
      return DispatchResult::NotFound;
    }
    url = ctx.stations->stationUrl(label);
  } else if (sound::isUrl(spec.text)) {
    url = spec.text;
    label = spec.text;
  } else {
    url = ctx.stations->stationUrl(spec.text);
    if (url.empty()) {
      ctx.detail = {"station", "unknown station"};
      return DispatchResult::NotFound;
    }
    label = spec.text;
  }
  const DispatchResult result = ctx.audio.playStream(url, label, ctx.detail);
  if (!ctx.detail.field.empty()) ctx.detail.field = "station";
  if (result == DispatchResult::Ok) {
    RuntimeState& runtime = ctx.state.runtime();
    runtime.radioPlaying = true;
    runtime.radioStation = label;
    runtime.radioTitle.clear();
    runtime.radioError.clear();
    ctx.state.emit(StateEvent::AudioChanged);
  }
  return result;
}

}

// The one place a Command becomes an effect. Holds no state of its own: everything it touches
// arrives through the CommandContext that CoreEngine fills in per call.
DispatchResult Dispatcher::dispatch(Command& cmd, CommandContext& ctx) {
  ctx.detail.clear();
  switch (cmd.type) {
    case CommandType::Notify:
      return ctx.notify.notify(cmd.payload, static_cast<uint8_t>(cmd.source), ctx.detail);
    case CommandType::DismissNotify:
      if (cmd.name.empty()) {
        ctx.notify.dismiss();
        return DispatchResult::Ok;
      }
      return ctx.notify.dismissNamed(cmd.name) ? DispatchResult::Ok : DispatchResult::NotFound;
    case CommandType::SetPushedApp:
      if (cmd.clear || api::isEmptyObject(cmd.payload)) {
        ctx.apps.deletePushedApp(cmd.name);
        return DispatchResult::Ok;
      }
      return ctx.apps.setPushedApp(cmd.name, cmd.payload, ctx.detail);
    case CommandType::SetAppOrder:
      return ctx.apps.setAppOrder(cmd.payload) ? DispatchResult::Ok : DispatchResult::ParseError;
    case CommandType::SetAppEnabled: {
      api::JsonReader value{std::string_view(cmd.payload)};
      api::JsonReader rest = value;
      bool on = false;
      if (!value.asBool(on) || !rest.skipValue() || !rest.atEnd()) {
        ctx.detail.message = "must be true or false";
        return DispatchResult::ValidationError;
      }
      ctx.apps.setAppEnabled(cmd.name, on);
      return DispatchResult::Ok;
    }
    case CommandType::SwitchApp:
      return ctx.apps.switchApp(cmd.name.empty() ? cmd.payload : cmd.name, ctx.detail);
    case CommandType::NextApp:
      ctx.apps.nextApp();
      return DispatchResult::Ok;
    case CommandType::PreviousApp:
      ctx.apps.previousApp();
      return DispatchResult::Ok;
    case CommandType::SetSettings:
      return applySettings(cmd.payload, ctx);
    case CommandType::SetIndicator:
      return applyIndicator(cmd.arg, cmd.payload, cmd.clear, ctx);
    case CommandType::Moodlight:
      return applyMoodlight(cmd.payload, cmd.clear, ctx);
    case CommandType::SetDisplay:
      return applyDisplay(cmd.payload, ctx);
    case CommandType::Sleep:
      return applySleep(cmd.payload, ctx);
    // Who asked decides the group: a script's own call is App, anything else an Alert.
    case CommandType::PlayAudio: {
      const bool script = cmd.source == Source::Internal && !cmd.name.empty();
      const sound::Origin origin = script ? sound::Origin::Script : sound::Origin::Play;
      sound::Choices choices;
      if (!sound::parse(cmd.payload, origin, choices, ctx.detail))
        return DispatchResult::ValidationError;
      if (choices.isStation()) return applyRadioPlay(choices.items[0], ctx);
      if (!ctx.audio.check(choices, origin, ctx.detail)) return DispatchResult::ValidationError;
      const sound::PlayResult result =
          static_cast<sound::PlayAs>(cmd.arg) == sound::PlayAs::Effect
              ? ctx.audio.playEffect(choices, cmd.name, ctx.detail)
              : ctx.audio.play(choices, script ? sound::Group::App : sound::Group::Alert,
                               cmd.name, ctx.detail);
      switch (result) {
        case sound::PlayResult::Ok:
          return DispatchResult::Ok;
        case sound::PlayResult::NotFound:
          return DispatchResult::NotFound;
        case sound::PlayResult::NoSink:
          return DispatchResult::Unavailable;
        case sound::PlayResult::Invalid:
          return DispatchResult::ValidationError;
      }
      return DispatchResult::Failed;
    }
    case CommandType::SetRadioStations:
      if (!ctx.stations) return DispatchResult::Failed;
      return ctx.stations->setStations(cmd.payload, ctx.detail);
    case CommandType::StopAudio: {
      const sound::Stop what = static_cast<sound::Stop>(cmd.arg);
      ctx.audio.stop(what, cmd.name);
      if (what == sound::Stop::All || what == sound::Stop::Radio) {
        ctx.state.runtime().radioPlaying = false;
        ctx.state.runtime().radioTitle.clear();
        ctx.state.emit(StateEvent::AudioChanged);
      }
      return DispatchResult::Ok;
    }
    case CommandType::ScriptSet:
      if (!ctx.scripts) {
        ctx.detail = {"", "scripting is off"};
        return DispatchResult::Failed;
      }
      return ctx.scripts->setScript(cmd.name, cmd.payload, ctx.detail);
    case CommandType::ScriptUpdate:
      if (!ctx.scripts) return DispatchResult::Unavailable;
      return ctx.scripts->updateScript(cmd.name, cmd.payload, ctx.detail);
    case CommandType::ScriptConfigSet:
      if (!ctx.scripts) {
        ctx.detail = {"", "scripting is off"};
        return DispatchResult::Failed;
      }
      return ctx.scripts->setScriptConfig(cmd.name, cmd.payload, ctx.detail);
    case CommandType::BuiltinAppConfigSet:
      return ctx.apps.setBuiltinAppConfig(cmd.name, cmd.payload, ctx.detail);
    case CommandType::ScriptDataSet:
      if (!ctx.scripts) {
        ctx.detail = {"", "scripting is off"};
        return DispatchResult::Failed;
      }
      return ctx.scripts->setScriptData(cmd.name, cmd.payload, ctx.detail);
    case CommandType::ScriptRemove:
      if (!ctx.scripts) {
        ctx.detail = {"", "scripting is off"};
        return DispatchResult::Failed;
      }
      ctx.scripts->removeScript(cmd.name);
      return DispatchResult::Ok;
    case CommandType::DeleteApp:
      ctx.apps.deletePushedApp(cmd.name);
      if (ctx.scripts) ctx.scripts->removeScript(cmd.name);
      return DispatchResult::Ok;
    case CommandType::Reboot:
      ctx.system.reboot();
      return DispatchResult::Ok;
    case CommandType::FactoryReset:
      ctx.system.factoryReset();
      return DispatchResult::Ok;
    case CommandType::ResetSettings:
      ctx.system.resetSettings();
      return DispatchResult::Ok;
    case CommandType::SendScreen:
      ctx.display.sendScreen();
      return DispatchResult::Ok;
    case CommandType::None:
    default:
      return DispatchResult::Unknown;
  }
}

}
