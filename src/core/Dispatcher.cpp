#include "core/Dispatcher.h"

#include <cctype>
#include <climits>
#include <string_view>

#include "core/JsonColor.h"
#include "core/StateStore.h"
#include "core/api/JsonCoerce.h"
#include "core/api/JsonReader.h"
#include "core/effects/EffectRegistry.h"
#include "core/payload/PayloadParser.h"
#include "core/render/Color.h"

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
  ctx.state.settings().applyRead(api::JsonReader(payload));
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
  if (clear || payload.empty() || payload == "{}") {
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
                    "must be a color (\"#RGB\", \"#RRGGBB\", [r,g,b], [\"HSV\",h,s,v] or a "
                    "packed integer)"};
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
  if (clear || payload.empty() || payload == "{}") {
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
                    "must be a color (\"#RGB\", \"#RRGGBB\", [r,g,b], [\"HSV\",h,s,v] or a "
                    "packed integer)"};
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
    ctx.detail = {"durationMs", "must be a positive integer (milliseconds)"};
    return DispatchResult::ValidationError;
  }
  ctx.state.runtime().matrixOff = true;
  ctx.state.emit(StateEvent::PowerChanged);
  ctx.system.sleep(static_cast<uint64_t>(ms));
  return DispatchResult::Ok;
}

DispatchResult applyRadioPlay(const Command& cmd, CommandContext& ctx) {
  if (!ctx.audio.caps().radio || !ctx.stations) {
    ctx.detail = {"", "this build has no audio output"};
    return DispatchResult::Unavailable;
  }

  api::JsonReader atStation, atIndex, atUrl;
  if (!api::readMembers(cmd.payload,
                        {{"station", &atStation}, {"index", &atIndex}, {"url", &atUrl}}))
    return DispatchResult::ParseError;

  // A station name wins, then an index into the station list, then a bare url; only one is used.
  std::string url;
  std::string label;
  std::string station, direct;
  const bool hasStation = atStation.isString() && atStation.appendString(station);
  const bool hasUrl = atUrl.isString() && atUrl.appendString(direct);
  if (hasStation) {
    url = ctx.stations->stationUrl(station);
    if (url.empty()) {
      ctx.detail = {"station", "unknown station"};
      return DispatchResult::NotFound;
    }
    label = station;
  } else if (api::present(atIndex)) {
    long long v = 0;
    if (!atIndex.isNumber() || !atIndex.isInteger() || !atIndex.asLong(v) || v < INT_MIN ||
        v > INT_MAX) {
      ctx.detail = {"index", "must be an integer"};
      return DispatchResult::ValidationError;
    }
    const int index = static_cast<int>(v);
    label = ctx.stations->stationNameAt(index);
    if (label.empty()) {
      ctx.detail = {"index", "no station at that position"};
      return DispatchResult::NotFound;
    }
    url = ctx.stations->stationUrl(label);
  } else if (hasUrl) {
    url = direct;
    if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) {
      ctx.detail = {"url", "must start with http:// or https://"};
      return DispatchResult::ValidationError;
    }
    label = url;
  } else {
    ctx.detail = {"station", "give a station name, an index or a url"};
    return DispatchResult::ValidationError;
  }

  const DispatchResult result = ctx.audio.playStream(url, label, ctx.detail);
  if (result == DispatchResult::Ok) {
    RuntimeState& runtime = ctx.state.runtime();
    runtime.radioPlaying = true;
    runtime.radioStation = label;
    runtime.radioTitle.clear();
    runtime.radioError.clear();
    ctx.state.emit(StateEvent::RadioChanged);
  }
  return result;
}

}

// The one place a Command becomes an effect. Holds no state of its own: everything it touches
// arrives through the CommandContext that CoreEngine fills in per call.
DispatchResult Dispatcher::dispatch(const Command& cmd, CommandContext& ctx) {
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
      if (cmd.clear || cmd.payload.empty() || cmd.payload == "{}") {
        ctx.apps.deletePushedApp(cmd.name);
        return DispatchResult::Ok;
      }
      return ctx.apps.setPushedApp(cmd.name, cmd.payload, ctx.detail);
    case CommandType::SetAppOrder:
      return ctx.apps.setAppOrder(cmd.payload) ? DispatchResult::Ok : DispatchResult::ParseError;
    case CommandType::SwitchApp:
      return ctx.apps.switchApp(cmd.name.empty() ? cmd.payload : cmd.name)
                 ? DispatchResult::Ok
                 : DispatchResult::NotFound;
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
    // The router owns the muting and writes its own detail message.
    case CommandType::PlayAudio:
      switch (ctx.audio.play(static_cast<sound::Source>(cmd.arg), cmd.payload, ctx.detail)) {
        case sound::PlayResult::Ok:
        case sound::PlayResult::Muted:
          return DispatchResult::Ok;
        case sound::PlayResult::NotFound:
          return DispatchResult::NotFound;
        case sound::PlayResult::NoSink:
          return DispatchResult::Unavailable;
        case sound::PlayResult::Invalid:
          return DispatchResult::ValidationError;
      }
      return DispatchResult::Failed;
    case CommandType::SetRadioStations:
      if (!ctx.stations) return DispatchResult::Failed;
      return ctx.stations->setStations(cmd.payload, ctx.detail);
    case CommandType::PlayStream:
      return applyRadioPlay(cmd, ctx);
    case CommandType::StopAudio: {
      const sound::StopScope scope = static_cast<sound::StopScope>(cmd.arg);
      ctx.audio.stop(scope);
      // Silencing one-shots leaves the stream, so the reported station survives too.
      if (scope != sound::StopScope::Sounds) {
        ctx.state.runtime().radioPlaying = false;
        ctx.state.runtime().radioTitle.clear();
        ctx.state.emit(StateEvent::RadioChanged);
      }
      return DispatchResult::Ok;
    }
    case CommandType::ScriptSet:
      if (!ctx.scripts) {
        ctx.detail = {"", "scripting is disabled (scriptingEnabled is off)"};
        return DispatchResult::Failed;
      }
      return ctx.scripts->setScript(cmd.name, cmd.payload, ctx.detail);
    case CommandType::ScriptConfigSet:
      if (!ctx.scripts) {
        ctx.detail = {"", "scripting is disabled (scriptingEnabled is off)"};
        return DispatchResult::Failed;
      }
      return ctx.scripts->setScriptConfig(cmd.name, cmd.payload, ctx.detail);
    case CommandType::ScriptRemove:
      if (!ctx.scripts) {
        ctx.detail = {"", "scripting is disabled (scriptingEnabled is off)"};
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
