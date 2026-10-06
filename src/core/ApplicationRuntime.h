#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "core/CoreEngine.h"
#include "core/api/CapabilitiesJson.h"
#include "core/launcher/Launcher.h"
#include "core/launcher/LauncherView.h"
#include "core/render/PowerAnimator.h"
#include "core/render/RenderPipeline.h"
#include "core/sensing/BrightnessPolicy.h"
#include "core/script/EngineScriptApplication.h"

namespace awtrix {
class IBoard;
namespace script { class ScriptHost; struct ScriptServices; }

// Borrowed services must outlive the runtime; the runtime owns the engine, app catalog and frames.
struct ApplicationServices {
  sound::AudioRouter& audio;
  IDisplayService& display;
  ISystemService& system;
  IPageClock& clock;
  const FontCatalog& fonts;
  IPageIcon* icons = nullptr;
  IPageIcon* iconsB = nullptr;
  std::function<void(uint8_t)> setBrightness;
  // Read every frame.
  std::function<LightConfig()> lightConfig{};
  IExternalPage* externalPage = nullptr;
  IContentSink* contentSink = nullptr;
  IPageZoom* pageZoom = nullptr;
};

enum class ApplicationTickStage { Audio, Engine, Scripts };
enum class PlatformFrame { None, Rendered, ExternalRate };

class ApplicationRuntime {
 public:
  using TickObserver = void (*)(ApplicationTickStage);
  using FrameOverride = PlatformFrame (*)(Canvas&, int64_t, void*);

  ApplicationRuntime(int width, int height, ApplicationServices services, AppRegistry apps);
  ~ApplicationRuntime();
  ApplicationRuntime(const ApplicationRuntime&) = delete;
  ApplicationRuntime& operator=(const ApplicationRuntime&) = delete;

  CoreEngine& engine() { return engine_; }
  // The menu behind a held select; the platform's button input drives it.
  launcher::Launcher& launcher() { return launcher_; }
  Canvas& canvas() { return canvas_; }
  AppRegistry& apps() { return apps_; }
  EffectRegistry& effects() { return effects_; }
  EffectRegistry& overlays() { return overlays_; }
  // Admission is performed before a platform registers its permanent LED output buffer.
  bool ready() { return canvas_.valid() && power_.ready() && pipeline_.prepareFrames(); }

  // Adds the shared engine bindings, preserving the caller's platform I/O services.
  void configureScripts(script::ScriptServices& services);
  void markSensors(IBoard& board);
  void applyOutputs(IBoard& board);
  void bindOutputs(IBoard& board, bool& settingsDirty);
  std::string capabilitiesJson(const PlatformDescriptor& platform,
                              const api::CapabilityMembersFn& members = nullptr) const;

  // Called before platform services so HTTP/MQTT see this frame's FPS sample and brightness.
  void beginFrame(int64_t nowMs);
  void tick(int64_t nowMs, script::ScriptHost* scripts = nullptr, TickObserver observer = nullptr);
  // Returns true when the platform frame source, such as Art-Net, supplies its own pacing.
  bool render(int64_t nowMs, FrameOverride platformFrame = nullptr, void* context = nullptr);
  bool powerBusy() const { return power_.busy(); }
  void invalidateIcons() { pipeline_.invalidateIcons(); }

 private:
  struct Effects;
  RenderPipelineDeps renderDeps();
  void updateBrightness(int64_t nowMs);
  void publishContent(const PageInfo* page, int64_t nowMs);

  ApplicationServices services_;
  std::unique_ptr<Effects> ownedEffects_;
  AppRegistry apps_;
  EffectRegistry effects_, overlays_;
  CoreEngine engine_;
  script::EngineScriptApplication scriptApplication_{engine_, services_.audio};
  launcher::Launcher launcher_{engine_};
  launcher::LauncherView launcherView_;
  Canvas canvas_;
  render::PowerAnimator power_;
  RenderPipeline pipeline_;
  BrightnessPolicy brightness_;
  int applied_ = -1;
  uint16_t frames_ = 0;
  int64_t windowStartMs_ = 0;
};

}
