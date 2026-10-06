#pragma once

#include "platform/tc002/update/PackageVerifier.h"
#include "platform/tc002/update/StateDocument.h"
#include "platform/tc002/update/StateStore.h"

namespace awtrix::tc002::update {

struct Outcome {
  bool ok = false;
  std::string code;   // Stable machine code, empty on success.
  std::string error;
};

// Update policy over verified packages and StateStore; no installation or clock.
// Caller serializes operations; commits require the document last read or written.
// A failed commit leaves the snapshot readable, but mutations require load() before reuse.
class UpdateState {
 public:
  explicit UpdateState(StateStore& store);
  // Reads, parses and reconciles in memory; never writes.
  Outcome load();
  // Creates the state file when it is absent.
  Outcome initialize(std::uint64_t acceptedCounter);
  bool loaded() const { return loaded_; }
  const Snapshot& snapshot() const { return snapshot_; }
  Outcome acquireLease(const std::string& owner, std::uint64_t now, std::uint64_t ttlSeconds);
  Outcome releaseLease(const std::string& owner, std::uint64_t now);
  Outcome stage(const std::string& owner, std::uint64_t now, const Result& verified,
                const FallbackAuthorization* authorization = nullptr);
  Outcome discard(const std::string& owner, std::uint64_t now);
  Outcome activate(const std::string& owner, std::uint64_t now, const Result& reverified);
  Outcome markBootPending(const std::string& owner, std::uint64_t now);
  Outcome confirm();
  Outcome rollback(const std::string& reason);

 private:
  Outcome leaseGuard(const std::string& owner, std::uint64_t now) const;
  Outcome commit(const Snapshot& next);
  StateStore& store_;
  Snapshot snapshot_;
  std::string document_;  // Bytes last read from or written to the store.
  bool loaded_ = false;
};

}  // namespace awtrix::tc002::update
