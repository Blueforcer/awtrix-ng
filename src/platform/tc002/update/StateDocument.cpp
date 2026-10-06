#include "platform/tc002/update/StateDocument.h"
#include "platform/tc002/update/StateRules.h"
#include "core/api/JsonReader.h"
#include "core/api/JsonWriter.h"

#include <algorithm>
#include <vector>

namespace awtrix::tc002::update {
using state_detail::hasCandidate;
using state_detail::validOwner;
using state_detail::validReason;
using state_detail::validRelease;
using state_detail::validStagedFile;

namespace {
struct StateName { State state; const char* name; };
constexpr StateName kStateNames[] = {
    {State::Idle, "idle"},           {State::Staged, "staged"},
    {State::Activating, "activating"}, {State::BootPending, "boot-pending"},
    {State::Confirmed, "confirmed"},   {State::RolledBack, "rolled-back"},
    {State::Quarantined, "quarantined"}};

void writeRelease(api::JsonWriter& w, const ReleaseRecord& record) {
  w.member("counter", static_cast<unsigned long long>(record.counter));
  w.member("payloadSha256", record.payloadSha256);
  w.member("release", record.release);
  w.member("target", record.target);
}

bool parseText(const api::JsonReader& reader, std::string& out) {
  if (!reader.isString()) return false;
  out.clear();
  return reader.appendString(out);
}

struct Slot { const char* name; api::JsonReader* reader; };

// The reader's key is the raw text between the quotes, which stay in the
// document, so the quoted string decodes in place.
bool memberName(const api::JsonReader& cursor, std::string& name) {
  const std::string_view raw = cursor.key();
  name.clear();
  return api::JsonReader(std::string_view(raw.data() - 1, raw.size() + 2)).appendString(name);
}

// Walks a well-formed value and everything nested in it. A name that appears
// twice in one object, compared after unescaping, is refused wherever it
// occurs, so the document has one reading for any JSON reader.
bool distinctNames(api::JsonReader value, std::string& error) {
  if (value.isArray()) {
    if (!value.enterArray()) return false;
    while (value.nextElement())
      if (!distinctNames(value, error) || !value.skipValue()) return false;
    return value.ok();
  }
  if (!value.isObject()) return true;
  if (!value.enterObject()) return false;
  std::vector<std::pair<std::string, std::string_view>> names;
  while (value.nextMember()) {
    names.emplace_back(std::string(), value.key());
    if (!memberName(value, names.back().first)) return false;
    if (!distinctNames(value, error) || !value.skipValue()) return false;
  }
  if (!value.ok()) return false;
  std::stable_sort(names.begin(), names.end(),
                   [](const auto& a, const auto& b) { return a.first < b.first; });
  const auto repeat = std::adjacent_find(names.begin(), names.end(),
                                         [](const auto& a, const auto& b) { return a.first == b.first; });
  if (repeat != names.end()) {
    error = "state document repeats member " + std::string((repeat + 1)->second);
    return false;
  }
  return true;
}

// Walks one object whose names distinctNames() accepted: a known member,
// matched by its unescaped name, lands in its slot; any other is skipped.
bool readObject(const api::JsonReader& object, const Slot* slots, std::size_t count,
                const char* where, std::string& error) {
  api::JsonReader cursor = object;
  if (!cursor.enterObject()) { error = std::string(where) + " is not readable"; return false; }
  std::string name;
  while (cursor.nextMember()) {
    if (!memberName(cursor, name)) { error = std::string(where) + " is not readable"; return false; }
    for (std::size_t i = 0; i < count; ++i) {
      if (name == slots[i].name) { *slots[i].reader = cursor; break; }
    }
    if (!cursor.skipValue()) { error = std::string(where) + " is not readable"; return false; }
  }
  if (!cursor.ok()) { error = std::string(where) + " is not readable"; return false; }
  return true;
}

bool memberCounter(const api::JsonReader& member, const char* name, std::uint64_t& out,
                   const char* where, std::string& error) {
  if (!api::present(member)) { error = std::string(where) + " lacks " + name; return false; }
  if (!member.asUnsigned(out)) { error = std::string(where) + "." + name + " is not an unsigned integer"; return false; }
  return true;
}

bool memberText(const api::JsonReader& member, const char* name, std::string& out,
                const char* where, std::string& error) {
  if (!api::present(member)) { error = std::string(where) + " lacks " + name; return false; }
  if (!parseText(member, out)) { error = std::string(where) + "." + name + " is not a string"; return false; }
  return true;
}

// Reads the release members of `object` and, when given, the candidate-only
// members in the same pass.
bool parseRelease(const api::JsonReader& object, ReleaseRecord& out, const char* where, bool legacy,
                  std::string& error, api::JsonReader* stagedFile = nullptr,
                  api::JsonReader* fallback = nullptr) {
  if (!object.isObject()) { error = std::string(where) + " must be an object or null"; return false; }
  api::JsonReader counter, keyId, payloadSha256, release, target;
  std::vector<Slot> slots{{"counter", &counter}, {"keyId", &keyId}, {"payloadSha256", &payloadSha256},
                          {"release", &release}, {"target", &target}};
  if (stagedFile) slots.push_back({"stagedFile", stagedFile});
  if (fallback) slots.push_back({"fallback", fallback});
  if (!readObject(object, slots.data(), slots.size(), where, error)) return false;
  if (!memberCounter(counter, "counter", out.counter, where, error) ||
      !memberText(payloadSha256, "payloadSha256", out.payloadSha256, where, error) ||
      !memberText(release, "release", out.release, where, error) ||
      !memberText(target, "target", out.target, where, error))
    return false;
  if (legacy) {
    std::string legacyKey;
    if (!memberText(keyId, "keyId", legacyKey, where, error)) return false;
    if (!isSha256Hex(legacyKey)) { error = std::string(where) + " has an invalid legacy key identifier"; return false; }
  }
  if (!validRelease(out)) { error = std::string(where) + " has an invalid release record"; return false; }
  return true;
}
}  // namespace

const char* stateName(State state) {
  for (const auto& entry : kStateNames) if (entry.state == state) return entry.name;
  return "";
}

bool stateFromName(std::string_view name, State& state) {
  for (const auto& entry : kStateNames) {
    if (name == entry.name) { state = entry.state; return true; }
  }
  return false;
}

bool operator==(const ReleaseRecord& a, const ReleaseRecord& b) {
  return a.counter == b.counter && a.payloadSha256 == b.payloadSha256 &&
         a.release == b.release && a.target == b.target;
}

bool operator==(const CandidateRecord& a, const CandidateRecord& b) {
  return a.identity == b.identity && a.stagedFile == b.stagedFile && a.fallback == b.fallback;
}

bool operator==(const LeaseRecord& a, const LeaseRecord& b) {
  return a.owner == b.owner && a.expiresAt == b.expiresAt;
}

bool operator==(const Snapshot& a, const Snapshot& b) {
  return a.state == b.state && a.acceptedCounter == b.acceptedCounter && a.current == b.current &&
         a.candidate == b.candidate && a.lease == b.lease && a.failure == b.failure;
}

bool operator!=(const Snapshot& a, const Snapshot& b) { return !(a == b); }

std::string serialize(const Snapshot& snapshot) {
  std::string out;
  out.reserve(1024);
  api::JsonWriter w(out);
  w.beginObject();
  w.member("schema", 2);
  w.member("state", stateName(snapshot.state));
  w.member("acceptedCounter", static_cast<unsigned long long>(snapshot.acceptedCounter));
  if (snapshot.current.counter == 0) {
    w.memberNull("current");
  } else {
    w.key("current").beginObject();
    writeRelease(w, snapshot.current);
    w.endObject();
  }
  if (snapshot.candidate.identity.counter == 0) {
    w.memberNull("candidate");
  } else {
    w.key("candidate").beginObject();
    writeRelease(w, snapshot.candidate.identity);
    w.member("stagedFile", snapshot.candidate.stagedFile);
    w.member("fallback", snapshot.candidate.fallback);
    w.endObject();
  }
  if (!snapshot.lease.held()) {
    w.memberNull("lease");
  } else {
    w.key("lease").beginObject();
    w.member("owner", snapshot.lease.owner);
    w.member("expiresAt", static_cast<unsigned long long>(snapshot.lease.expiresAt));
    w.endObject();
  }
  w.member("failure", snapshot.failure);
  w.endObject();
  return out;
}

bool parse(std::string_view json, Snapshot& out, std::string& error) {
  error.clear();
  if (json.size() > kMaxStateBytes) { error = "state document exceeds the size limit"; return false; }
  if (!api::isWellFormed(json)) { error = "state document is not well-formed JSON"; return false; }
  const api::JsonReader root(json);
  if (!root.isObject()) { error = "state document must be an object"; return false; }
  if (!distinctNames(root, error)) {
    if (error.empty()) error = "state document is not readable";
    return false;
  }
  api::JsonReader schema, state, accepted, current, candidate, lease, failure;
  const Slot members[] = {{"schema", &schema},       {"state", &state}, {"acceptedCounter", &accepted},
                          {"current", &current},     {"candidate", &candidate}, {"lease", &lease},
                          {"failure", &failure}};
  constexpr char where[] = "state document";
  if (!readObject(root, members, sizeof(members) / sizeof(members[0]), where, error)) return false;
  for (const auto& member : members) {
    if (!api::present(*member.reader)) { error = std::string(where) + " lacks " + member.name; return false; }
  }
  Snapshot result;
  if (!schema.isNumber() || (schema.valueText() != "1" && schema.valueText() != "2")) {
    error = "unsupported state schema";
    return false;
  }
  const bool legacy = schema.valueText() == "1";
  std::string stateText;
  if (!parseText(state, stateText) || !stateFromName(stateText, result.state)) {
    error = "unknown state name";
    return false;
  }
  if (!accepted.asUnsigned(result.acceptedCounter)) { error = "acceptedCounter is not an unsigned integer"; return false; }
  if (!current.isNull() && !parseRelease(current, result.current, "current", legacy, error)) return false;
  if (!candidate.isNull()) {
    api::JsonReader stagedFile, fallback;
    if (!parseRelease(candidate, result.candidate.identity, "candidate", legacy, error, &stagedFile, &fallback)) return false;
    if (!memberText(stagedFile, "stagedFile", result.candidate.stagedFile, "candidate", error)) return false;
    if (!validStagedFile(result.candidate.stagedFile)) { error = "candidate.stagedFile is invalid"; return false; }
    if (!api::present(fallback) || !fallback.asBool(result.candidate.fallback)) {
      error = "candidate.fallback is not a boolean";
      return false;
    }
  }
  if (!lease.isNull()) {
    if (!lease.isObject()) { error = "lease must be an object or null"; return false; }
    api::JsonReader owner, expiresAt;
    const Slot leaseMembers[] = {{"owner", &owner}, {"expiresAt", &expiresAt}};
    if (!readObject(lease, leaseMembers, 2, "lease", error)) return false;
    if (!memberText(owner, "owner", result.lease.owner, "lease", error) ||
        !memberCounter(expiresAt, "expiresAt", result.lease.expiresAt, "lease", error))
      return false;
    if (!validOwner(result.lease.owner)) { error = "lease.owner is invalid"; return false; }
  }
  if (!parseText(failure, result.failure)) { error = "failure is not a string"; return false; }
  if (!result.failure.empty() && !validReason(result.failure)) { error = "failure text is invalid"; return false; }

  const bool candidatePresent = result.candidate.identity.counter != 0;
  if (candidatePresent != hasCandidate(result.state)) {
    error = candidatePresent ? "candidate present in a quiescent state" : "candidate missing";
    return false;
  }
  if (result.state == State::Confirmed && result.current.counter == 0) {
    error = "confirmed state without a current release";
    return false;
  }
  if (result.current.counter > result.acceptedCounter) {
    error = "current release above the accepted counter";
    return false;
  }
  if (candidatePresent) {
    const auto counter = result.candidate.identity.counter;
    if (!result.candidate.fallback && counter <= result.acceptedCounter) {
      error = "candidate at or below the accepted counter without fallback";
      return false;
    }
    if (result.candidate.fallback && counter > result.acceptedCounter) {
      error = "fallback candidate above the accepted counter";
      return false;
    }
    if (counter == result.current.counter) {
      error = "candidate equals the current release";
      return false;
    }
  }
  out = result;
  return true;
}


}  // namespace awtrix::tc002::update
