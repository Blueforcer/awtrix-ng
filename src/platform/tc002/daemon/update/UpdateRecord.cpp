#include "platform/tc002/daemon/update/UpdateRecord.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>

#include "platform/posix/Text.h"

#include "platform/tc002/update/UpdateState.h"
#include "platform/tc002/update/FileStateStore.h"

namespace awtrix {
namespace tc002d {
namespace update = tc002::update;
namespace {

using update::FileStateStore;
using update::Outcome;
using update::State;
using update::UpdateState;

constexpr std::size_t kMaxErrorText = 256;

std::string describe(const Outcome& outcome) { return "update state " + outcome.code + ": " + outcome.error; }

void tightenStateFile(const std::string& directory) {
  const std::string path = directory + "/update-state.json";
  struct stat info{};
  if (::lstat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode) && info.st_uid == ::geteuid() &&
      (info.st_mode & 077) != 0)
    ::chmod(path.c_str(), 0600);
}

update::Result candidateResult(const update::CandidateRecord& candidate) {
  update::Result result;
  result.ok = true;
  result.counter = candidate.identity.counter;
  result.payloadSha256 = candidate.identity.payloadSha256;
  result.release = candidate.identity.release;
  result.target = candidate.identity.target;
  result.stagedFile = candidate.stagedFile;
  return result;
}

Outcome activateStaged(UpdateState& state, uint64_t now) {
  const Outcome lease = state.acquireLease(UpdateRecord::kOwner, now, UpdateRecord::kLeaseSeconds);
  if (!lease.ok) return lease;
  return state.activate(UpdateRecord::kOwner, now, candidateResult(state.snapshot().candidate));
}

}

struct UpdateRecord::Session {
  explicit Session(const std::string& directory) : store(directory), state(store) {
    loaded = store.ok() ? state.load() : Outcome{false, "storage", store.error()};
  }
  FileStateStore store;
  UpdateState state;
  Outcome loaded;
};

UpdateRecord::UpdateRecord(std::string stateDir, uint64_t runningCounter, std::function<uint64_t()> clock)
    : stateDir_(std::move(stateDir)), runningCounter_(runningCounter), clock_(std::move(clock)) {}

UpdateRecord::~UpdateRecord() = default;

UpdateRecord::Session& UpdateRecord::open(std::unique_ptr<Session>& temporary) const {
  if (held_) return *held_;
  temporary = std::make_unique<Session>(stateDir_);
  return *temporary;
}

uint64_t UpdateRecord::now() const {
  if (clock_) return clock_();
  const std::time_t wall = std::time(nullptr);
  return wall > 0 ? static_cast<uint64_t>(wall) : 0;
}

std::string UpdateRecord::failureText(const std::string& release, const std::string& reason) {
  return posix::printable(release.empty() ? reason : release + ": " + reason, kMaxErrorText);
}

tc002::UpdateStatus UpdateRecord::status(std::string* error) const {
  tightenStateFile(stateDir_);
  Session session(stateDir_);
  if (!session.loaded.ok) {
    if (session.loaded.code == "uninitialized") return {"idle", "", ""};
    if (error) *error = describe(session.loaded);
    return {"idle", "", posix::printable("update state unreadable: " + session.loaded.error, kMaxErrorText)};
  }
  const update::Snapshot& snapshot = session.state.snapshot();
  const std::string& candidate = snapshot.candidate.identity.release;
  switch (snapshot.state) {
    case State::Idle: return {"idle", snapshot.current.release, ""};
    case State::Staged:
    case State::Activating: return {"applying", candidate, ""};
    case State::BootPending: return {"boot-pending", candidate, ""};
    case State::Confirmed: return {"confirmed", snapshot.current.release, ""};
    case State::Quarantined: return {"failed", candidate, "the installation was interrupted"};
    case State::RolledBack: break;
  }
  const std::string& failure = snapshot.failure;
  const auto split = failure.find(": ");
  if (split != std::string::npos && split > 0 && split <= 64 && failure.find(' ') > split)
    return {"failed", failure.substr(0, split), failure.substr(split + 2)};
  return {"failed", "", failure};
}

bool UpdateRecord::stage(const PackageHeader& header, const std::string& package, std::string& error) {
  tightenStateFile(stateDir_);
  Session session(stateDir_);
  if (!session.loaded.ok && session.loaded.code == "uninitialized")
    session.loaded = session.state.initialize(runningCounter_);
  if (!session.loaded.ok) {
    error = describe(session.loaded);
    return false;
  }
  const uint64_t at = now();
  Outcome outcome = session.state.acquireLease(kOwner, at, kLeaseSeconds);
  if (outcome.ok) {
    update::Result verified;
    verified.ok = true;
    verified.target = header.target;
    verified.release = header.release;
    verified.payloadSha256 = header.payloadSha256;
    verified.counter = header.counter;
    verified.payloadBytes = header.payloadBytes;
    verified.stagedFile = package;
    outcome = session.state.stage(kOwner, at, verified);
  }
  if (!outcome.ok) error = describe(outcome);
  return outcome.ok;
}

bool UpdateRecord::begin(StagedUpdate& out, std::string& error) {
  held_.reset();
  auto session = std::make_unique<Session>(stateDir_);
  if (!session->loaded.ok) {
    error = describe(session->loaded);
    return false;
  }
  if (session->state.snapshot().state != State::Staged) {
    error = std::string("no update is staged (state ") + update::stateName(session->state.snapshot().state) + ")";
    return false;
  }
  const Outcome outcome = activateStaged(session->state, now());
  if (!outcome.ok) {
    error = describe(outcome);
    return false;
  }
  held_ = std::move(session);
  const update::CandidateRecord& candidate = held_->state.snapshot().candidate;
  out.release = candidate.identity.release;
  out.counter = candidate.identity.counter;
  out.payloadSha256 = candidate.identity.payloadSha256;
  out.target = candidate.identity.target;
  out.package = candidate.stagedFile;
  return true;
}

void UpdateRecord::close() { held_.reset(); }

bool UpdateRecord::bootPending(std::string& error) {
  std::unique_ptr<Session> temporary;
  Session& session = open(temporary);
  Outcome outcome = session.loaded;
  if (outcome.ok) outcome = session.state.markBootPending(kOwner, now());
  if (!outcome.ok) error = describe(outcome);
  return outcome.ok;
}

bool UpdateRecord::confirm(std::string& error) {
  std::unique_ptr<Session> temporary;
  Session& session = open(temporary);
  Outcome outcome = session.loaded;
  if (outcome.ok) outcome = session.state.confirm();
  if (!outcome.ok) error = describe(outcome);
  return outcome.ok;
}

bool UpdateRecord::fail(const std::string& release, const std::string& reason, std::string& error) {
  std::unique_ptr<Session> temporary;
  Session& session = open(temporary);
  Outcome outcome = session.loaded;
  if (outcome.ok && session.state.snapshot().state == State::Staged) outcome = activateStaged(session.state, now());
  if (outcome.ok) outcome = session.state.rollback(failureText(release, reason));
  if (!outcome.ok) error = describe(outcome);
  return outcome.ok;
}

BootAction UpdateRecord::reconcile(const RunningRelease& running, const std::string& writerResult,
                                   std::string& candidateRelease, std::string& note) {
  candidateRelease.clear();
  note.clear();
  tightenStateFile(stateDir_);
  auto session = std::make_unique<Session>(stateDir_);
  if (!session->loaded.ok && session->loaded.code == "corrupt") {
    const std::string path = stateDir_ + "/update-state.json";
    note = "unreadable update state moved to update-state.json.corrupt (" + session->loaded.error + "); ";
    session.reset();
    if (std::rename(path.c_str(), (path + ".corrupt").c_str()) != 0) {
      note += std::string("cannot move it: ") + std::strerror(errno);
      return BootAction::None;
    }
    session = std::make_unique<Session>(stateDir_);
  }
  if (!session->loaded.ok && session->loaded.code == "uninitialized") {
    const Outcome created = session->state.initialize(runningCounter_);
    note += created.ok ? "update state created with accepted counter " + std::to_string(runningCounter_)
                       : describe(created);
    return BootAction::None;
  }
  if (!session->loaded.ok) {
    note += describe(session->loaded);
    return BootAction::None;
  }
  UpdateState& state = session->state;
  const update::Snapshot snapshot = state.snapshot();
  const std::string candidate = snapshot.candidate.identity.release;
  Outcome outcome{true, {}, {}};
  BootAction action = BootAction::None;
  switch (snapshot.state) {
    case State::Staged:
      outcome = activateStaged(state, now());
      if (outcome.ok) outcome = state.rollback(failureText(candidate, "the installation did not start"));
      note += "staged update " + candidate + " never started";
      break;
    case State::Activating:
    case State::Quarantined:
      outcome = state.rollback(failureText(candidate, "the installation was interrupted"));
      note += "installation of " + candidate + " was interrupted";
      break;
    case State::BootPending:
      candidateRelease = candidate;
      if (running.release == candidate &&
          (running.counter == 0 || running.counter == snapshot.candidate.identity.counter)) {
        note += "release " + candidate + " waits for confirmation";
        action = BootAction::Confirm;
      } else {
        const std::string why = writerResult.empty() || writerResult.compare(0, 5, "done:") == 0
                                    ? "release " + running.release + " runs instead"
                                    : "the release slot was not written (" + writerResult + ")";
        outcome = state.rollback(failureText(candidate, why));
        note += "boot-pending release " + candidate + " is not running: " + why;
      }
      break;
    case State::Idle:
    case State::Confirmed:
    case State::RolledBack:
      break;
  }
  if (!outcome.ok) note += "; " + describe(outcome);
  return action;
}

}
}
