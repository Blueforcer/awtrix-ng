#pragma once

#include "core/ApplicationRuntime.h"
#include "core/ScreenDisplay.h"
#include "core/SettingsSaver.h"
#include "core/render/PaletteStore.h"
#include "core/script/IScriptFiles.h"
#include "core/script/ScriptSlots.h"
#include "core/script/RestoreScripts.h"
#include "media/ScriptIcon.h"
#include "persistence/AppOrderStore.h"
#include "persistence/NvsSettings.h"
#include "persistence/RadioStore.h"
#include "system/Log.h"
#include "system/MonotonicClock.h"
#include "transport/ScriptMqttBridge.h"
#include "transport/mqtt/MqttService.h"
#include "transport/net/MirrorLink.h"

namespace awtrix::bootstrap {

inline void restoreUserState(CoreEngine& engine) {
  nvs::loadSettings(engine.state().settings());
  apporder::load(engine);
  engine.setOrderPersist(apporder::save);
  radiostore::load(engine);
  engine.setStationPersist(radiostore::save);
}

inline void assetsChanged(ApplicationRuntime& app, ScriptIcon& icons) {
  icons.invalidate();
  app.invalidateIcons();
  render::clearPaletteCache();
}

inline void mirrorServices(ApplicationServices& services, MirrorLink& link) {
  services.externalPage = &link.mirror();
  services.contentSink = &link.mirror();
}

inline void bindDisplay(ScreenDisplay& display, Canvas& canvas, MqttService& mqtt) {
  display.setScreen(&canvas);
  display.setPublisher([&mqtt](const std::string& topic, const std::string& payload) {
    mqtt.publish(topic, payload, false);
  });
}

template <class... Options>
void beginMqtt(MqttService& mqtt, CoreEngine& engine, IBoard& board, DeviceConfig& cfg,
               const std::string& uid, net::IHostResolver& resolver, Options... options) {
  mqtt.begin(engine, board, cfg, uid, uid, cfg.hostname.empty() ? "AWTRIX NG" : cfg.hostname,
             resolver, options...);
}

template <class Files>
void configureScriptFiles(script::ScriptServices& services, Files& files, ScriptIcon& icons) {
  services.icon = &icons;
  services.storeSink = &files;
  services.readSource = [&files](const std::string& name, std::string& out) {
    return files.readSource(name, out);
  };
  services.readStore = [&files](const std::string& name, std::string& out) {
    return files.readStore(name, out);
  };
  services.monotonicMs = [] { return monotonicMs(); };
  services.log = [](const std::string& text) { logf("%s", text.c_str()); };
  services.logDebug = [](const std::string& text) { logdbg("%s", text.c_str()); };
}

inline void bindScriptMqtt(script::ScriptHost& scripts, MqttService& mqtt, ScriptMqttBridge& bridge) {
  bridge.begin([&mqtt](const std::string& topic, const std::string& body) { mqtt.publishRaw(topic, body); },
      [&mqtt](const std::string& topic) { mqtt.subscribeRaw(topic); },
      [&mqtt](const std::string& topic) { mqtt.unsubscribeRaw(topic); },
      [&scripts](script::MqttMessage message) { scripts.pushMqttMessage(std::move(message)); });
  mqtt.setScriptBridge(&bridge);
}

template <class Worker>
void bindScriptHttp(script::ScriptHost& scripts, Worker& worker) {
  worker.begin([&scripts](script::HttpResult result) { scripts.pushHttpResult(std::move(result)); });
}

inline void restoreScripts(script::ScriptHost& scripts, script::IScriptFiles& files, int64_t staggerMs) {
  script::restoreScripts(scripts, files, staggerMs, [](const std::string& name, const std::string& reason) {
    logf("scripts: %s not restored (%s)", name.c_str(), reason.c_str());
  });
  if (scripts.count() && staggerMs > 0)
    logf("scripts: %u restored", static_cast<unsigned>(scripts.count()));
}

template <class Buttons>
void bindScriptButtons(Buttons& buttons, CoreEngine& engine, script::ScriptHost* scripts) {
  buttons.setButtonHook([&engine, scripts](int button, bool pressed, bool held) {
    return scripts && scripts->handleButtonState(engine.currentAppId(), button, pressed, held);
  });
}

}
