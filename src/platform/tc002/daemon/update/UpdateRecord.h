#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "platform/tc002/contract/SupervisorProtocol.h"
#include "platform/tc002/daemon/update/Package.h"
#include "platform/tc002/update/ReleaseManifest.h"

// state/update-state.json through UpdateState (src/platform/tc002/update). Every call opens the
// store, works on a fresh load and closes it again, so the daemon never holds the directory lock
// while it waits for something; only an install keeps the store open from begin() on, because a
// fresh load reads an activation as interrupted. A failure is stored as "<release>: <reason>".
namespace awtrix {
namespace tc002d {

using tc002::update::RunningRelease;

struct StagedUpdate {
  std::string release;
  uint64_t counter = 0;
  std::string payloadSha256;
  std::string target;
  std::string package;
};

enum class BootAction { None, Confirm };

class UpdateRecord {
 public:
  static constexpr const char* kOwner = "awtrix-tc002d";
  static constexpr uint64_t kLeaseSeconds = 3600;

  explicit UpdateRecord(std::string stateDir, uint64_t runningCounter = 0,
                        std::function<uint64_t()> clock = {});
  ~UpdateRecord();
  UpdateRecord(const UpdateRecord&) = delete;
  UpdateRecord& operator=(const UpdateRecord&) = delete;

  const std::string& stateDir() const { return stateDir_; }
  tc002::UpdateStatus status(std::string* error = nullptr) const;

  bool stage(const PackageHeader& header, const std::string& package, std::string& error);
  // Staged -> activating; out is the staged candidate. The store stays open until the object goes.
  bool begin(StagedUpdate& out, std::string& error);
  bool bootPending(std::string& error);
  // Closes the store begin() keeps open.
  void close();
  bool confirm(std::string& error);
  bool fail(const std::string& release, const std::string& reason, std::string& error);
  // Runs before the services start. A boot-pending candidate that runs waits for confirm, one that
  // does not failed; writerResult, what awtrix-tc002-flash recorded, says why. An update that never
  // got to the flash helper fails.
  BootAction reconcile(const RunningRelease& running, const std::string& writerResult,
                       std::string& candidateRelease, std::string& note);

  static std::string failureText(const std::string& release, const std::string& reason);

 private:
  struct Session;
  Session& open(std::unique_ptr<Session>& temporary) const;
  uint64_t now() const;
  std::unique_ptr<Session> held_;
  std::string stateDir_;
  uint64_t runningCounter_;
  std::function<uint64_t()> clock_;
};

}
}
