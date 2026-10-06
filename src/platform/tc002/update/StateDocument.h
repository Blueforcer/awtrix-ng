#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace awtrix::tc002::update {

enum class State { Idle, Staged, Activating, BootPending, Confirmed, RolledBack, Quarantined };

const char* stateName(State state);
bool stateFromName(std::string_view name, State& state);

constexpr std::size_t kMaxStateBytes = 65536;
constexpr std::size_t kMaxOwnerBytes = 64;
constexpr std::size_t kMaxReasonBytes = 256;
constexpr std::size_t kMaxStagedFileBytes = 4096;
constexpr std::uint64_t kMaxLeaseSeconds = 86400;

struct ReleaseRecord {
  std::uint64_t counter = 0;  // 0 means no release recorded.
  std::string payloadSha256;
  std::string release;
  std::string target;
};

struct CandidateRecord {
  ReleaseRecord identity;
  std::string stagedFile;
  bool fallback = false;
};

struct LeaseRecord {
  std::string owner;
  std::uint64_t expiresAt = 0;
  bool held() const { return !owner.empty(); }
};

struct FallbackAuthorization {
  std::uint64_t counter = 0;
  std::string payloadSha256;
};

struct Snapshot {
  State state = State::Idle;
  std::uint64_t acceptedCounter = 0;  // High-water mark; changes only in confirm().
  ReleaseRecord current;              // Last confirmed release; counter 0 until the first confirm.
  CandidateRecord candidate;          // Present exactly in staged/activating/boot-pending/quarantined.
  LeaseRecord lease;
  std::string failure;                // Reason recorded by rollback() or reconciliation.
};

bool operator==(const ReleaseRecord& a, const ReleaseRecord& b);
bool operator==(const CandidateRecord& a, const CandidateRecord& b);
bool operator==(const LeaseRecord& a, const LeaseRecord& b);
bool operator==(const Snapshot& a, const Snapshot& b);
bool operator!=(const Snapshot& a, const Snapshot& b);

// Canonical one-line document with a fixed member order (schema 2).
std::string serialize(const Snapshot& snapshot);
// Strict parse: all known members required, unknown members ignored, names
// distinct within every object, every consistency rule of the state machine
// enforced.
bool parse(std::string_view json, Snapshot& out, std::string& error);


}  // namespace awtrix::tc002::update
