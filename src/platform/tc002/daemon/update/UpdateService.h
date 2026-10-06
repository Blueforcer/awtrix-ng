#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core/api/JsonWriter.h"
#include "platform/tc002/contract/SupervisorProtocol.h"
#include "platform/tc002/daemon/Service.h"
#include "platform/tc002/daemon/update/Package.h"
#include "platform/tc002/daemon/update/UpdatePaths.h"
#include "platform/tc002/daemon/update/UpdateRecord.h"

// The daemon's side of a web update (docs/developers/tc002/index.md, Web update, flow steps 4 to
// 6): takes the runtime's UpdateReady, stages it, copies the running release's flash helper into
// RAM and asks for an orderly stop, after which main marks the update boot-pending and execs that
// copy to write the release slot and reboot. A new release is confirmed once its runtime was
// healthy.
namespace awtrix {
namespace tc002d {

struct UpdateServiceOptions {
  InstallPaths paths;
  // The running release; its bin/awtrix-tc002-flash writes the next one.
  std::string root;
  // Bytes of release image the slot takes; 0 turns the web update off.
  uint64_t capacity = 0;
  RunningRelease running;
  bool confirmPending = false;
  std::string candidate;
  // The loader's start counters of the candidate: cleared by the confirmation, not by runtime health.
  std::vector<std::string> bootAttemptsPaths;
  int64_t checkIntervalMs = 5000;
};

struct UpdateLinks {
  // Ordinary bounded PCM capture drains during shutdown; only a firmware operation blocks OTA.
  std::function<bool()> mcuFirmwareIdle;
  std::function<bool()> runtimeHealthy;
  std::function<void()> statusChanged;
  std::function<void(const std::string& reason)> stop;
};

struct ExecPlan {
  std::string executable;
  std::vector<std::string> arguments;
};

class UpdateService : public Service {
 public:
  static constexpr const char* kFlashHelper = "awtrix-tc002-flash";

  UpdateService(UpdateServiceOptions options, UpdateLinks links);

  const char* name() const override { return "update"; }
  bool start(int64_t nowMs) override;
  int64_t nextDeadlineMs() const override;
  void onTime(int64_t nowMs) override;
  void requestStop(int64_t nowMs) override;

  // From the runtime; false when the update was refused (the reason is in the next hello).
  bool handOff(const tc002::UpdateReady& ready);
  bool handOffPending() const { return handOff_; }
  // After the services stopped: records the staged update as boot-pending, the state the next
  // daemon settles, and returns the flash helper's command line; false when it cannot.
  bool prepareExec(ExecPlan& plan, std::string& error);
  // The exec failed: the update fails and the running release stays.
  void execFailed(const std::string& reason);
  std::string helloDatagram() const;
  void appendStatus(api::JsonWriter& json) const;
  const tc002::UpdateStatus& status() const { return status_; }
  bool confirming() const { return confirming_; }
  void refreshStatus();

 private:
  void confirmed();

  UpdateServiceOptions options_;
  UpdateLinks links_;
  UpdateRecord record_;
  tc002::UpdateStatus status_;
  bool handOff_ = false;
  std::string package_;
  PackageHeader header_;
  std::string helper_;
  bool confirming_ = false;
  int64_t nextCheck_ = -1;
};

// release and counter from <root>/manifest.json; empty and 0 when unreadable.
using tc002::update::readRunningRelease;

}
}
