#include "platform/tc002/update/UpdateState.h"
#include "platform/tc002/update/StateRules.h"

#include <utility>

namespace awtrix::tc002::update {
using state_detail::hasCandidate;
using state_detail::validOwner;
using state_detail::validReason;
using state_detail::validRelease;
using state_detail::validStagedFile;

namespace {
Outcome success() { return {true, {}, {}}; }
Outcome refuse(const char* code, const std::string& error) { return {false, code, error}; }
Outcome illegal(State state, const char* operation) {
  return refuse("illegal-transition", std::string(operation) + " is not allowed in state " + stateName(state));
}
}  // namespace

UpdateState::UpdateState(StateStore& store) : store_(store) {}

Outcome UpdateState::load() {
  loaded_ = false;
  std::string json, error;
  bool exists = false;
  if (!store_.read(json, exists, error)) return refuse("storage", error);
  if (!exists) return refuse("uninitialized", "state file is absent");
  Snapshot parsed;
  if (!parse(json, parsed, error)) return refuse("corrupt", error);
  // An activation that never reached boot-pending is reconciled in memory only;
  // the next successful write records the quarantine.
  if (parsed.state == State::Activating) {
    parsed.state = State::Quarantined;
    parsed.failure = "activation interrupted";
  }
  snapshot_ = parsed;
  document_ = std::move(json);
  loaded_ = true;
  return success();
}

Outcome UpdateState::initialize(std::uint64_t acceptedCounter) {
  std::string json, error;
  bool exists = false;
  if (!store_.read(json, exists, error)) {
    loaded_ = false;
    return refuse("storage", error);
  }
  if (exists) return refuse("already-initialized", "state file exists");
  Snapshot next;
  next.acceptedCounter = acceptedCounter;
  std::string document = serialize(next);
  if (!store_.write(document, error)) {
    loaded_ = false;
    return refuse("storage", error);
  }
  snapshot_ = next;
  document_ = std::move(document);
  loaded_ = true;
  return success();
}

Outcome UpdateState::commit(const Snapshot& next) {
  std::string stored, error;
  bool exists = false;
  if (!store_.read(stored, exists, error)) {
    loaded_ = false;
    return refuse("storage", error);
  }
  if (!exists || stored != document_) {
    loaded_ = false;
    return refuse("stale", "state file changed since this object last read or wrote it");
  }
  std::string document = serialize(next);
  if (!store_.write(document, error)) {
    loaded_ = false;
    return refuse("storage", error);
  }
  snapshot_ = next;
  document_ = std::move(document);
  return success();
}

Outcome UpdateState::leaseGuard(const std::string& owner, std::uint64_t now) const {
  if (!validOwner(owner)) return refuse("invalid-input", "invalid lease owner");
  if (!snapshot_.lease.held()) return refuse("lease-required", "no lease is held");
  if (snapshot_.lease.owner != owner) return refuse("lease-held", "lease is held by " + snapshot_.lease.owner);
  if (now >= snapshot_.lease.expiresAt) return refuse("lease-expired", "lease has expired");
  return success();
}

Outcome UpdateState::acquireLease(const std::string& owner, std::uint64_t now, std::uint64_t ttlSeconds) {
  if (!loaded_) return refuse("not-loaded", "state is not loaded");
  if (!validOwner(owner)) return refuse("invalid-input", "invalid lease owner");
  if (ttlSeconds == 0 || ttlSeconds > kMaxLeaseSeconds || now > UINT64_MAX - ttlSeconds)
    return refuse("invalid-input", "invalid lease duration");
  const auto& lease = snapshot_.lease;
  if (lease.held() && lease.owner != owner && now < lease.expiresAt)
    return refuse("lease-held", "lease is held by " + lease.owner);
  Snapshot next = snapshot_;
  next.lease = {owner, now + ttlSeconds};
  return commit(next);
}

Outcome UpdateState::releaseLease(const std::string& owner, std::uint64_t now) {
  if (!loaded_) return refuse("not-loaded", "state is not loaded");
  if (!validOwner(owner)) return refuse("invalid-input", "invalid lease owner");
  const auto& lease = snapshot_.lease;
  if (!lease.held()) return refuse("lease-required", "no lease is held");
  if (lease.owner != owner && now < lease.expiresAt)
    return refuse("lease-held", "lease is held by " + lease.owner);
  Snapshot next = snapshot_;
  next.lease = {};
  return commit(next);
}

Outcome UpdateState::stage(const std::string& owner, std::uint64_t now, const Result& verified,
                           const FallbackAuthorization* authorization) {
  if (!loaded_) return refuse("not-loaded", "state is not loaded");
  if (const auto guard = leaseGuard(owner, now); !guard.ok) return guard;
  const State state = snapshot_.state;
  if (state != State::Idle && state != State::Confirmed && state != State::RolledBack)
    return refuse("illegal-transition", std::string("update in progress in state ") + stateName(state));
  if (!verified.ok) return refuse("invalid-input", "package is not verified");
  const ReleaseRecord identity{verified.counter, verified.payloadSha256,
                               verified.release, verified.target};
  if (!validRelease(identity)) return refuse("invalid-input", "verifier result has invalid identity fields");
  if (!validStagedFile(verified.stagedFile)) return refuse("invalid-input", "invalid staged file path");
  if (identity.counter == snapshot_.current.counter)
    return refuse("duplicate", "package carries the counter of the current release");
  if (!authorization) {
    if (snapshot_.acceptedCounter == UINT64_MAX)
      return refuse("counter-maximum", "accepted counter is at its maximum");
    if (identity.counter <= snapshot_.acceptedCounter)
      return refuse("downgrade", "package counter is not above the accepted counter");
  } else if (authorization->counter != identity.counter ||
             authorization->payloadSha256 != identity.payloadSha256 ||
             authorization->counter > snapshot_.acceptedCounter) {
    return refuse("authorization-mismatch", "fallback authorization does not name this package");
  }
  Snapshot next = snapshot_;
  next.state = State::Staged;
  next.candidate.identity = identity;
  next.candidate.stagedFile = verified.stagedFile;
  next.candidate.fallback = authorization != nullptr;
  next.failure.clear();
  return commit(next);
}

Outcome UpdateState::discard(const std::string& owner, std::uint64_t now) {
  if (!loaded_) return refuse("not-loaded", "state is not loaded");
  if (const auto guard = leaseGuard(owner, now); !guard.ok) return guard;
  if (snapshot_.state != State::Staged) return illegal(snapshot_.state, "discard");
  Snapshot next = snapshot_;
  next.state = State::Idle;
  next.candidate = {};
  return commit(next);
}

Outcome UpdateState::activate(const std::string& owner, std::uint64_t now, const Result& reverified) {
  if (!loaded_) return refuse("not-loaded", "state is not loaded");
  if (const auto guard = leaseGuard(owner, now); !guard.ok) return guard;
  if (snapshot_.state != State::Staged) return illegal(snapshot_.state, "activate");
  const ReleaseRecord identity{reverified.counter, reverified.payloadSha256,
                               reverified.release, reverified.target};
  if (!reverified.ok || !(identity == snapshot_.candidate.identity))
    return refuse("invalid-input", "re-verified package differs from the staged candidate");
  Snapshot next = snapshot_;
  next.state = State::Activating;
  return commit(next);
}

Outcome UpdateState::markBootPending(const std::string& owner, std::uint64_t now) {
  if (!loaded_) return refuse("not-loaded", "state is not loaded");
  if (const auto guard = leaseGuard(owner, now); !guard.ok) return guard;
  if (snapshot_.state != State::Activating) return illegal(snapshot_.state, "markBootPending");
  Snapshot next = snapshot_;
  next.state = State::BootPending;
  return commit(next);
}

// Runs after a boot; the installer that held the lease may be gone and there
// is no trusted time, so no lease is required and the lease is cleared.
Outcome UpdateState::confirm() {
  if (!loaded_) return refuse("not-loaded", "state is not loaded");
  if (snapshot_.state != State::BootPending) return illegal(snapshot_.state, "confirm");
  Snapshot next = snapshot_;
  next.state = State::Confirmed;
  next.current = snapshot_.candidate.identity;
  if (next.current.counter > next.acceptedCounter) next.acceptedCounter = next.current.counter;
  next.candidate = {};
  next.lease = {};
  next.failure.clear();
  return commit(next);
}

Outcome UpdateState::rollback(const std::string& reason) {
  if (!loaded_) return refuse("not-loaded", "state is not loaded");
  const State state = snapshot_.state;
  if (state != State::Activating && state != State::BootPending && state != State::Quarantined)
    return illegal(state, "rollback");
  if (!validReason(reason)) return refuse("invalid-input", "rollback reason must be 1..256 printable ASCII bytes");
  Snapshot next = snapshot_;
  next.state = State::RolledBack;
  next.candidate = {};
  next.lease = {};
  next.failure = reason;
  return commit(next);
}
}  // namespace awtrix::tc002::update
