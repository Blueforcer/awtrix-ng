// CTest executable for the update state policy: transitions, counter acceptance,
// lease, fallback authorization, codec corruption, per-step storage faults and
// stale objects sharing one store.
#include "platform/tc002/update/UpdateState.h"
#include "platform/tc002/update/FileStateStore.h"
#include "core/api/JsonReader.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace update = awtrix::tc002::update;
namespace fs = std::filesystem;
using update::State;

#define CHECK(expression) do { if (!(expression)) throw std::runtime_error("line " + std::to_string(__LINE__) + ": " #expression); } while (0)

namespace {

void testSharedTargetVectors(const char* path) {
  std::ifstream file(path);
  CHECK(file.good());
  const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
  awtrix::api::JsonReader list(text);
  CHECK(list.enterArray());
  int cases = 0;
  while (list.nextElement()) {
    bool expected = false;
    CHECK(awtrix::api::memberValue(list, "allowed").asBool(expected));
    const auto target = awtrix::api::memberText(list, "target");
    CHECK(update::allowedTarget(target) == expected);
    CHECK(list.skipValue());
    ++cases;
  }
  CHECK(list.ok() && cases > 0);
}

class MemoryStateStore final : public update::StateStore {
 public:
  std::string json;
  bool exists = false;
  bool failWrite = false;
  bool failRead = false;
  int writes = 0;
  bool read(std::string& out, bool& present, std::string& error) override {
    if (failRead) { error = "injected read failure"; return false; }
    out = json;
    present = exists;
    return true;
  }
  bool write(const std::string& value, std::string& error) override {
    if (failWrite) { error = "injected write failure"; return false; }
    json = value;
    exists = true;
    ++writes;
    return true;
  }
};

class TempDir {
 public:
  TempDir() {
    const char* base = std::getenv("TMPDIR");
    std::string pattern = std::string(base && *base ? base : "/tmp") + "/awtrix-update-state-XXXXXX";
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');
    if (!::mkdtemp(buffer.data())) throw std::runtime_error("mkdtemp failed");
    path_ = buffer.data();
    if (::chmod(path_.c_str(), 0700) != 0) throw std::runtime_error("chmod failed");
  }
  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path_, ec);
  }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;
  const std::string& path() const { return path_; }
 private:
  std::string path_;
};

std::string hex64(char fill, std::uint64_t counter) {
  constexpr char digits[] = "0123456789abcdef";
  std::string value(48, fill);
  for (int shift = 60; shift >= 0; shift -= 4) value += digits[(counter >> shift) & 15];
  return value;
}

update::Result sample(std::uint64_t counter) {
  update::Result result;
  result.ok = true;
  result.counter = counter;
  result.payloadSha256 = hex64('b', counter);
  result.release = "1.0." + std::to_string(counter);
  result.target = "experimental:test-fixture";
  result.payloadBytes = 16;
  result.stagedFile = "/stage/" + std::to_string(counter) + ".awup";
  return result;
}

void testStateNames() {
  const State states[] = {State::Idle, State::Staged, State::Activating, State::BootPending,
                          State::Confirmed, State::RolledBack, State::Quarantined};
  const char* names[] = {"idle", "staged", "activating", "boot-pending", "confirmed",
                         "rolled-back", "quarantined"};
  for (std::size_t i = 0; i < 7; ++i) {
    CHECK(std::string(update::stateName(states[i])) == names[i]);
    State parsed = State::Confirmed;
    CHECK(update::stateFromName(names[i], parsed));
    CHECK(parsed == states[i]);
  }
  State parsed = State::Idle;
  CHECK(!update::stateFromName("Idle", parsed));
  CHECK(!update::stateFromName("", parsed));
  CHECK(!update::stateFromName("boot_pending", parsed));
  CHECK(!update::stateFromName("staged ", parsed));
}

update::Snapshot populated() {
  update::Snapshot s;
  s.state = State::Staged;
  s.acceptedCounter = UINT64_MAX;
  s.current = {UINT64_MAX, hex64('b', 7), "1.0.7", "experimental:test-fixture"};
  s.candidate.identity = {3, hex64('b', 3), "1.0.3", "experimental:test-fixture"};
  s.candidate.stagedFile = "/stage/3.awup";
  s.candidate.fallback = true;
  s.lease = {"installer:1", UINT64_MAX};
  s.failure = "";
  return s;
}

void testCodecRoundTrip() {
  const auto original = populated();
  const std::string text = update::serialize(original);
  CHECK(text.find('\n') == std::string::npos);
  const std::string expected =
      "{\"schema\":2,\"state\":\"staged\",\"acceptedCounter\":18446744073709551615,"
      "\"current\":{\"counter\":18446744073709551615,\"payloadSha256\":\"" + hex64('b', 7) + "\",\"release\":\"1.0.7\",\"target\":\"experimental:test-fixture\"},"
      "\"candidate\":{\"counter\":3,\"payloadSha256\":\"" + hex64('b', 3) +
      "\",\"release\":\"1.0.3\",\"target\":\"experimental:test-fixture\",\"stagedFile\":\"/stage/3.awup\",\"fallback\":true},"
      "\"lease\":{\"owner\":\"installer:1\",\"expiresAt\":18446744073709551615},\"failure\":\"\"}";
  CHECK(text == expected);
  update::Snapshot parsed;
  std::string error;
  CHECK(update::parse(text, parsed, error));
  CHECK(error.empty());
  CHECK(parsed == original);
  CHECK(!(parsed != original));

  update::Snapshot idle;
  idle.acceptedCounter = 5;
  idle.failure = "boot \"failed\"";
  const std::string idleText = update::serialize(idle);
  CHECK(idleText == "{\"schema\":2,\"state\":\"idle\",\"acceptedCounter\":5,\"current\":null,"
                    "\"candidate\":null,\"lease\":null,\"failure\":\"boot \\\"failed\\\"\"}");
  update::Snapshot idleParsed;
  CHECK(update::parse(idleText, idleParsed, error));
  CHECK(idleParsed == idle);

  update::Snapshot rolledBack;
  rolledBack.state = State::RolledBack;
  rolledBack.acceptedCounter = 9;
  rolledBack.current = {9, hex64('b', 9), "1.0.9", "experimental:test-fixture"};
  rolledBack.failure = "activation interrupted";
  update::Snapshot rolledBackParsed;
  CHECK(update::parse(update::serialize(rolledBack), rolledBackParsed, error));
  CHECK(rolledBackParsed == rolledBack);
}

// Replaces the first occurrence of `from` in the canonical text of `snapshot`.
std::string mutate(const update::Snapshot& snapshot, const std::string& from, const std::string& to) {
  std::string text = update::serialize(snapshot);
  const auto at = text.find(from);
  CHECK(at != std::string::npos);
  return text.replace(at, from.size(), to);
}

void rejects(const std::string& text, const char* label) {
  update::Snapshot out;
  std::string error;
  if (update::parse(text, out, error) || error.empty())
    throw std::runtime_error(std::string("parse accepted ") + label + ": " + text);
}


void testLegacyStateMigration() {
  const std::string legacy =
      "{\"schema\":1,\"state\":\"confirmed\",\"acceptedCounter\":7,"
      "\"current\":{\"counter\":7,\"keyId\":\"" + hex64('a', 7) +
      "\",\"payloadSha256\":\"" + hex64('b', 7) + "\",\"release\":\"1.0.7\","
      "\"target\":\"awtrix-ng:tc002\"},\"candidate\":null,\"lease\":null,\"failure\":\"\"}";
  update::Snapshot parsed;
  std::string error;
  CHECK(update::parse(legacy, parsed, error));
  CHECK(parsed.acceptedCounter == 7 && parsed.current.counter == 7);
  CHECK(parsed.current.payloadSha256 == hex64('b', 7));
  const std::string migrated = update::serialize(parsed);
  CHECK(migrated.find("\"schema\":2") != std::string::npos);
  CHECK(migrated.find("keyId") == std::string::npos);
  update::Snapshot roundTrip;
  CHECK(update::parse(migrated, roundTrip, error));
  CHECK(roundTrip == parsed);
  std::string malformed = legacy;
  malformed.erase(malformed.find(hex64('a', 7)), 1);
  CHECK(!update::parse(malformed, roundTrip, error));

  MemoryStateStore store;
  store.exists = true;
  store.json = legacy;
  const auto accepted = store.json.find("\"acceptedCounter\":7");
  CHECK(accepted != std::string::npos);
  store.json.replace(accepted, std::strlen("\"acceptedCounter\":7"),
                     "\"acceptedCounter\":18446744073709551615");
  update::UpdateState state(store);
  CHECK(state.load().ok);
  CHECK(store.writes == 0);
  CHECK(state.snapshot().acceptedCounter == UINT64_MAX);
  CHECK(state.acquireLease("installer", 1, 100).ok);
  CHECK(store.json.find("\"schema\":2") != std::string::npos);
  CHECK(store.json.find("keyId") == std::string::npos);
  update::UpdateState reloaded(store);
  CHECK(reloaded.load().ok);
  CHECK(reloaded.snapshot().acceptedCounter == UINT64_MAX);
  CHECK(reloaded.snapshot().current.counter == 7);
  CHECK(reloaded.stage("installer", 2, sample(8)).code == "counter-maximum");
}

void testCodecRejections() {
  const auto full = populated();
  const std::string text = update::serialize(full);
  rejects(text.substr(0, text.size() - 1), "truncated document");
  rejects(text + "x", "trailing bytes");
  rejects(text + " {}", "trailing document");
  rejects("[" + text + "]", "array top level");
  rejects("", "empty document");
  rejects(mutate(full, "\"schema\":2", "\"schema\":3"), "schema 3");
  rejects(mutate(full, "\"schema\":2", "\"schema\":\"1\""), "schema as string");
  rejects(mutate(full, "\"schema\":2", "\"schema\":2.0"), "schema 2.0");
  for (const char* member : {"\"schema\":2,", "\"state\":\"staged\",", "\"acceptedCounter\":18446744073709551615,",
                             ",\"lease\":{\"owner\":\"installer:1\",\"expiresAt\":18446744073709551615}",
                             ",\"failure\":\"\""})
    rejects(mutate(full, member, ""), "missing member");
  rejects(mutate(full, "\"current\":{", "\"currents\":{"), "missing current");
  rejects(mutate(full, "\"candidate\":{", "\"candidates\":{"), "missing candidate");
  rejects(mutate(full, "\"state\":\"staged\"", "\"state\":\"Staged\""), "unknown state name");
  rejects(mutate(full, "\"state\":\"staged\"", "\"state\":\"boot_pending\""), "unknown state name");
  rejects(mutate(full, "\"state\":\"staged\"", "\"state\":null"), "null state");
  for (const char* counter : {"-1", "1.5", "1e3", "01", "18446744073709551616", "123456789012345678901",
                              "\"5\"", "null", "true", "-0", "0x10", "+1"})
    rejects(mutate(full, "\"acceptedCounter\":18446744073709551615", std::string("\"acceptedCounter\":") + counter),
            "invalid counter");
  rejects(mutate(full, "\"counter\":3,", "\"counter\":0,"), "candidate counter 0");
  rejects(mutate(full, "\"expiresAt\":18446744073709551615", "\"expiresAt\":\"abc\""), "expiresAt string");
  rejects(mutate(full, "\"expiresAt\":18446744073709551615", "\"expiresAt\":-5"), "expiresAt negative");
  rejects(mutate(full, hex64('b', 3), hex64('g', 3)), "non-hex payload hash");
  rejects(mutate(full, "\"release\":\"1.0.7\"", "\"release\":\"1.0/7\""), "release with slash");
  rejects(mutate(full, "\"release\":\"1.0.7\"", "\"release\":\"\""), "empty release");
  rejects(mutate(full, "\"release\":\"1.0.7\"", "\"release\":\"1.0:7\""), "release with colon");
  rejects(mutate(full, "\"release\":\"1.0.7\"", "\"release\":\"-1.0.7\""), "release starting with a hyphen");
  rejects(mutate(full, "\"release\":\"1.0.7\"", "\"release\":\"1.0.7.partial\""), "release still being written");
  {
    update::Snapshot parsed;
    std::string error;
    CHECK(update::parse(mutate(full, "\"release\":\"1.0.7\"", "\"release\":\"1.0.7+tc002\""), parsed, error));
    CHECK(parsed.current.release == "1.0.7+tc002");
  }
  rejects(mutate(full, "\"target\":\"experimental:test-fixture\"", "\"target\":\"tc002\""), "production target");
  rejects(mutate(full, "\"target\":\"experimental:test-fixture\"", "\"target\":\"experimental:\""), "bare prefix target");
  rejects(mutate(full, "\"stagedFile\":\"/stage/3.awup\"", "\"stagedFile\":\"\""), "empty stagedFile");
  rejects(mutate(full, "\"stagedFile\":\"/stage/3.awup\"", "\"stagedFile\":\"/a\\u0000b\""), "NUL in stagedFile");
  rejects(mutate(full, "\"stagedFile\":\"/stage/3.awup\",", ""), "missing stagedFile");
  rejects(mutate(full, "\"fallback\":true", "\"fallback\":\"true\""), "fallback as string");
  rejects(mutate(full, ",\"fallback\":true", ""), "missing fallback");
  rejects(mutate(full, "\"owner\":\"installer:1\"", "\"owner\":\"installer 1\""), "owner with space");
  rejects(mutate(full, "\"owner\":\"installer:1\"", "\"owner\":\"\""), "empty owner");
  rejects(mutate(full, "\"owner\":\"installer:1\"", "\"owner\":\"" + std::string(65, 'o') + "\""), "65-byte owner");
  rejects(mutate(full, "\"lease\":{", "\"lease\":["), "lease as array");
  rejects(mutate(full, "\"failure\":\"\"", "\"failure\":\"tab\\there\""), "control character in failure");
  rejects(mutate(full, "\"failure\":\"\"", "\"failure\":\"" + std::string(257, 'f') + "\""), "257-byte failure");
  rejects(mutate(full, "\"failure\":\"\"", "\"failure\":null"), "null failure");

  // Consistency between members.
  update::Snapshot idle;
  idle.acceptedCounter = 5;
  rejects(mutate(idle, "\"state\":\"idle\"", "\"state\":\"staged\""), "candidate missing in staged");
  rejects(mutate(idle, "\"state\":\"idle\"", "\"state\":\"confirmed\""), "confirmed without current");
  rejects(mutate(full, "\"state\":\"staged\"", "\"state\":\"idle\""), "candidate present in idle");
  rejects(mutate(full, "\"state\":\"staged\"", "\"state\":\"confirmed\""), "candidate present in confirmed");
  rejects(mutate(full, "\"state\":\"staged\"", "\"state\":\"rolled-back\""), "candidate present in rolled-back");
  update::Snapshot confirmed;
  confirmed.state = State::Confirmed;
  confirmed.acceptedCounter = 5;
  confirmed.current = {5, hex64('b', 5), "1.0.5", "experimental:test-fixture"};
  rejects(mutate(confirmed, "\"acceptedCounter\":5", "\"acceptedCounter\":4"), "current above accepted");
  update::Snapshot staged = confirmed;
  staged.state = State::Staged;
  staged.candidate.identity = {6, hex64('b', 6), "1.0.6", "experimental:test-fixture"};
  staged.candidate.stagedFile = "/stage/6.awup";
  rejects(mutate(staged, "\"acceptedCounter\":5", "\"acceptedCounter\":6"), "non-fallback candidate at accepted");
  rejects(mutate(staged, "\"acceptedCounter\":5", "\"acceptedCounter\":7"), "non-fallback candidate below accepted");
  rejects(mutate(staged, "\"fallback\":false", "\"fallback\":true"), "fallback candidate above accepted");
  rejects(mutate(staged, "\"counter\":6,", "\"counter\":5,"), "candidate equals current");
  update::Snapshot fallbackStaged = staged;
  fallbackStaged.acceptedCounter = 9;
  fallbackStaged.candidate.fallback = true;
  rejects(mutate(fallbackStaged, "\"counter\":6,", "\"counter\":5,"), "fallback candidate equals current");

  // A member name repeated within one object is refused at every level, even when
  // the repeated value is identical and therefore consistent.
  rejects(mutate(full, "\"schema\":2,", "\"schema\":2,\"schema\":2,"), "repeated schema");
  rejects(mutate(full, "\"acceptedCounter\":18446744073709551615,",
                 "\"acceptedCounter\":18446744073709551615,\"acceptedCounter\":18446744073709551615,"),
          "repeated acceptedCounter");
  rejects(mutate(full, "\"failure\":\"\"", "\"failure\":\"\",\"failure\":\"\""), "repeated failure");
  rejects(mutate(full, "\"counter\":18446744073709551615,", "\"counter\":18446744073709551615,\"counter\":18446744073709551615,"),
          "repeated current counter");
  rejects(mutate(full, "\"counter\":3,", "\"counter\":3,\"counter\":3,"), "repeated candidate counter");
  rejects(mutate(full, "\"fallback\":true", "\"fallback\":true,\"fallback\":true"), "repeated fallback");
  rejects(mutate(full, "\"expiresAt\":18446744073709551615", "\"expiresAt\":18446744073709551615,\"expiresAt\":18446744073709551615"),
          "repeated expiresAt");
  rejects(mutate(full, "\"owner\":\"installer:1\",", "\"owner\":\"installer:1\",\"owner\":\"installer:1\","), "repeated owner");
  rejects(mutate(full, "\"schema\":2,", "\"schema\":2,\"vendor\":1,\"vendor\":1,"), "repeated unknown member");
  rejects(mutate(full, "\"stagedFile\":\"/stage/3.awup\",", "\"stagedFile\":\"/stage/3.awup\",\"note\":{},\"note\":{},"),
          "repeated unknown candidate member");
  // Inside the value of an unknown member too, in objects and array elements at any depth.
  rejects(mutate(full, "\"schema\":2,", "\"schema\":2,\"vendor\":{\"x\":1,\"x\":2},"), "repeat inside unknown object");
  rejects(mutate(full, "\"schema\":2,", "\"schema\":2,\"vendor\":[{\"a\":1,\"a\":1}],"), "repeat inside array element");
  rejects(mutate(full, "\"schema\":2,", "\"schema\":2,\"vendor\":[1,[{\"b\":{\"c\":{},\"c\":[]}}]],"), "repeat deep in arrays");
  rejects(mutate(full, "\"stagedFile\":", "\"note\":{\"deep\":{\"k\":0,\"k\":0}},\"stagedFile\":"),
          "repeat inside unknown candidate member");
  rejects(mutate(full, "\"expiresAt\":", "\"extra\":[{\"k\":null,\"k\":null}],\"expiresAt\":"),
          "repeat inside unknown lease member");
  rejects(mutate(full, "\"counter\":18446744073709551615,", "\"counter\":18446744073709551615,\"x\":{\"y\":1,\"y\":1},"),
          "repeat inside unknown current member");
  // Names compare after unescaping, so an escaped spelling is the same member.
  rejects(mutate(full, "\"schema\":2,", "\"schema\":2,\"\\u0073chema\":1,"), "escaped repeat of schema");
  rejects(mutate(full, "\"counter\":3,", "\"counter\":3,\"\\u0063ounter\":4,"), "escaped repeat of candidate counter");
  rejects(mutate(full, "\"schema\":2,", "\"schema\":2,\"vendor\":{\"x\":1,\"\\u0078\":2},"), "escaped repeat inside unknown object");

  // Unknown members are ignored at every level; whitespace is accepted.
  update::Snapshot out;
  std::string error;
  CHECK(update::parse(mutate(full, "\"schema\":2,", "\"schema\":2,\"vendor\":{\"nested\":[1,2,{}]},"), out, error));
  CHECK(out == full);
  // The same name in different objects is legal; only repeats within one object are not.
  CHECK(update::parse(mutate(full, "\"schema\":2,", "\"schema\":2,\"vendor\":{\"schema\":2,\"counter\":0},"), out, error));
  CHECK(out == full);
  CHECK(update::parse(mutate(full, "\"fallback\":true", "\"fallback\":true,\"lease\":{\"owner\":\"x\"}"), out, error));
  CHECK(out == full);
  CHECK(update::parse(mutate(full, "\"fallback\":true", "\"fallback\":true,\"note\":\"x\""), out, error));
  CHECK(out == full);
  CHECK(update::parse(mutate(full, "\"expiresAt\":", "\"extra\":false,\"expiresAt\":"), out, error));
  CHECK(out == full);
  CHECK(update::parse(mutate(full, "\"schema\":2,", "\"schema\":2,\"vendor\":[{\"a\":1},{\"a\":1},{\"a\":{\"a\":{\"a\":1}}}],"), out, error));
  CHECK(out == full);
  // A known member spelled with escapes is that member.
  CHECK(update::parse(mutate(full, "\"counter\":3,", "\"\\u0063ounter\":3,"), out, error));
  CHECK(out == full);
  CHECK(update::parse(mutate(full, "\"schema\":2,", "\"sch\\u0065ma\":2,"), out, error));
  CHECK(out == full);
  CHECK(update::parse(" " + mutate(full, "\"schema\":2,", "\"schema\" : 2 ,\n") + "\n", out, error));
  CHECK(out == full);
  CHECK(update::parse(mutate(fallbackStaged, "\"fallback\":true", "\"fallback\":true"), out, error));
  CHECK(out == fallbackStaged);
  CHECK(update::parse(mutate(staged, "\"stagedFile\":\"/stage/6.awup\"", "\"stagedFile\":\"/st\\u00e4ge/6.awup\""), out, error));
  CHECK(out.candidate.stagedFile == "/st\xc3\xa4ge/6.awup");
  rejects(std::string("{") + std::string(update::kMaxStateBytes, ' ') + "}", "oversized document");

  // Nesting is bounded: a value enclosed by more than 15 objects or arrays, the
  // root object counted, is refused as not well-formed even inside an unknown member.
  const auto arrays = [](int levels, const char* innermost) {
    return "\"schema\":2,\"vendor\":" + std::string(levels, '[') + innermost + std::string(levels, ']') + ",";
  };
  CHECK(update::parse(mutate(full, "\"schema\":2,", arrays(15, "")), out, error));
  CHECK(out == full);
  rejects(mutate(full, "\"schema\":2,", arrays(16, "")), "container enclosed by 16 containers");
  CHECK(update::parse(mutate(full, "\"schema\":2,", arrays(14, "1")), out, error));
  CHECK(out == full);
  rejects(mutate(full, "\"schema\":2,", arrays(15, "1")), "scalar enclosed by 16 containers");
  rejects(mutate(full, "\"schema\":2,", arrays(15, "{}")), "object enclosed by 16 containers");
}

void expectCode(const update::Outcome& outcome, const char* code) {
  if (outcome.ok || outcome.code != code || outcome.error.empty())
    throw std::runtime_error(std::string("expected ") + code + ", got " +
                             (outcome.ok ? "ok" : outcome.code) + ": " + outcome.error);
}

void expectOk(const update::Outcome& outcome) {
  if (!outcome.ok || !outcome.code.empty() || !outcome.error.empty())
    throw std::runtime_error("expected success, got " + outcome.code + ": " + outcome.error);
}

// Every mutating operation, for the not-loaded and matrix checks.
struct Operation {
  const char* name;
  std::function<update::Outcome(update::UpdateState&)> run;
};

std::vector<Operation> operations(const std::string& owner, std::uint64_t now) {
  return {
      {"acquireLease", [=](update::UpdateState& s) { return s.acquireLease(owner, now, 60); }},
      {"releaseLease", [=](update::UpdateState& s) { return s.releaseLease(owner, now); }},
      {"stage", [=](update::UpdateState& s) { return s.stage(owner, now, sample(1)); }},
      {"discard", [=](update::UpdateState& s) { return s.discard(owner, now); }},
      {"activate", [=](update::UpdateState& s) { return s.activate(owner, now, sample(1)); }},
      {"markBootPending", [=](update::UpdateState& s) { return s.markBootPending(owner, now); }},
      {"confirm", [](update::UpdateState& s) { return s.confirm(); }},
      {"rollback", [](update::UpdateState& s) { return s.rollback("boot failed"); }},
  };
}

void testInitialize() {
  MemoryStateStore store;
  update::UpdateState state(store);
  expectCode(state.load(), "uninitialized");
  CHECK(!state.loaded());
  for (const auto& op : operations("installer", 10)) {
    expectCode(op.run(state), "not-loaded");
    CHECK(store.writes == 0);
    CHECK(!store.exists);
  }
  expectOk(state.initialize(5));
  CHECK(state.loaded());
  CHECK(store.writes == 1);
  update::Snapshot expected;
  expected.acceptedCounter = 5;
  CHECK(state.snapshot() == expected);
  CHECK(store.json == update::serialize(expected));
  expectCode(state.initialize(7), "already-initialized");
  CHECK(store.writes == 1);
  CHECK(state.snapshot() == expected);

  update::UpdateState second(store);
  expectCode(second.initialize(7), "already-initialized");
  CHECK(!second.loaded());
  expectOk(second.load());
  CHECK(second.loaded());
  CHECK(second.snapshot() == expected);
  CHECK(store.writes == 1);

  MemoryStateStore corrupt;
  corrupt.exists = true;
  corrupt.json = "{\"schema\":2}";
  update::UpdateState third(corrupt);
  expectCode(third.load(), "corrupt");
  CHECK(!third.loaded());
  for (const auto& op : operations("installer", 10)) expectCode(op.run(third), "not-loaded");
  expectCode(third.initialize(1), "already-initialized");
  CHECK(corrupt.writes == 0);
  CHECK(corrupt.json == "{\"schema\":2}");

  MemoryStateStore unreadable;
  unreadable.failRead = true;
  update::UpdateState fourth(unreadable);
  expectCode(fourth.load(), "storage");
  expectCode(fourth.initialize(1), "storage");
  CHECK(!fourth.loaded());
  CHECK(unreadable.writes == 0);

  MemoryStateStore unwritable;
  unwritable.failWrite = true;
  update::UpdateState fifth(unwritable);
  expectCode(fifth.initialize(1), "storage");
  CHECK(!fifth.loaded());
  CHECK(!unwritable.exists);
}

update::ReleaseRecord release(std::uint64_t counter) {
  const auto r = sample(counter);
  return {r.counter, r.payloadSha256, r.release, r.target};
}

// Confirmed release `current` with a lease held by "installer" until t=1000.
update::Snapshot confirmedAt(std::uint64_t current) {
  update::Snapshot s;
  s.state = State::Confirmed;
  s.acceptedCounter = current;
  s.current = release(current);
  s.lease = {"installer", 1000};
  return s;
}

update::UpdateState loadFrom(MemoryStateStore& store, const update::Snapshot& snapshot) {
  store.json = update::serialize(snapshot);
  store.exists = true;
  store.writes = 0;
  update::UpdateState state(store);
  expectOk(state.load());
  return state;
}

void testLease() {
  MemoryStateStore store;
  update::UpdateState state(store);
  expectOk(state.initialize(0));
  expectOk(state.acquireLease("A", 0, 10));
  CHECK(state.snapshot().lease == (update::LeaseRecord{"A", 10}));
  expectCode(state.acquireLease("B", 5, 10), "lease-held");
  expectCode(state.acquireLease("B", 9, 10), "lease-held");
  const std::string expiredForeign = store.json;
  expectCode(state.stage("B", 10, sample(1)), "lease-held");
  expectCode(state.stage("B", 50, sample(1)), "lease-held");
  CHECK(store.json == expiredForeign);
  expectOk(state.acquireLease("B", 10, 10));
  CHECK(state.snapshot().lease == (update::LeaseRecord{"B", 20}));
  expectCode(state.stage("A", 12, sample(1)), "lease-held");
  expectCode(state.releaseLease("A", 12), "lease-held");
  expectOk(state.releaseLease("B", 12));
  CHECK(!state.snapshot().lease.held());
  expectCode(state.releaseLease("B", 12), "lease-required");
  expectOk(state.acquireLease("A", 12, 10));
  expectOk(state.acquireLease("A", 15, 10));
  CHECK(state.snapshot().lease == (update::LeaseRecord{"A", 25}));
  expectCode(state.releaseLease("B", 24), "lease-held");
  expectOk(state.releaseLease("B", 25));
  expectOk(state.acquireLease("C", 25, 86400));
  CHECK(state.snapshot().lease == (update::LeaseRecord{"C", 86425}));
  expectOk(state.acquireLease("C", 26, 1));
  CHECK(state.snapshot().lease == (update::LeaseRecord{"C", 27}));

  const auto before = state.snapshot();
  const int writes = store.writes;
  expectCode(state.acquireLease("C", 30, 0), "invalid-input");
  expectCode(state.acquireLease("C", 30, 86401), "invalid-input");
  expectCode(state.acquireLease("C", UINT64_MAX - 5, 10), "invalid-input");
  expectOk(state.acquireLease("C", UINT64_MAX - 10, 10));
  CHECK(state.snapshot().lease.expiresAt == UINT64_MAX);
  expectOk(state.acquireLease("C", 30, 1));
  for (const char* owner : {"", "a b", "a/b", "a\tb", "a\x7f", "\xc3\xa4"})
    expectCode(state.acquireLease(owner, 30, 10), "invalid-input");
  expectCode(state.acquireLease(std::string(65, 'o'), 30, 10), "invalid-input");
  expectOk(state.acquireLease(std::string(64, 'o'), 31, 10));
  expectCode(state.releaseLease("", 31), "invalid-input");
  expectCode(state.releaseLease("a b", 31), "invalid-input");
  CHECK(store.writes == writes + 3);
  CHECK(before.acceptedCounter == state.snapshot().acceptedCounter);

  // A lease left in the file by an earlier process: another owner waits for its expiry,
  // while any caller passing the same owner string renews or releases it at once.
  MemoryStateStore left;
  update::Snapshot staged = confirmedAt(5);
  staged.state = State::Staged;
  staged.candidate.identity = release(6);
  staged.candidate.stagedFile = "/stage/6.awup";
  update::UpdateState afterReboot = loadFrom(left, staged);
  expectCode(afterReboot.acquireLease("other", 0, 60), "lease-held");
  expectCode(afterReboot.releaseLease("other", 999), "lease-held");
  expectCode(afterReboot.discard("other", 999), "lease-held");
  CHECK(left.writes == 0);
  expectOk(afterReboot.acquireLease("installer", 0, 60));
  CHECK(afterReboot.snapshot().lease == (update::LeaseRecord{"installer", 60}));
  expectOk(afterReboot.releaseLease("installer", 0));
  CHECK(!afterReboot.snapshot().lease.held());
  update::UpdateState later = loadFrom(left, staged);
  expectOk(later.acquireLease("other", 1000, 60));
  expectOk(later.discard("other", 1001));
}

void testStageGuards() {
  MemoryStateStore store;
  update::UpdateState state(store);
  expectOk(state.initialize(0));
  expectOk(state.acquireLease("A", 0, 100));
  const auto validation = [&](update::Result r, const char* label) {
    const std::string bytes = store.json;
    const auto o = state.stage("A", 1, r);
    if (o.ok || o.code != "invalid-input") throw std::runtime_error(std::string("stage accepted ") + label);
    CHECK(store.json == bytes);
  };
  { auto r = sample(1); r.ok = false; validation(r, "ok=false"); }
  { auto r = sample(1); r.counter = 0; validation(r, "counter 0"); }
  { auto r = sample(1); r.payloadSha256 = ""; validation(r, "empty payload hash"); }
  { auto r = sample(1); r.payloadSha256 = r.payloadSha256.substr(1); validation(r, "63-char payload hash"); }
  { auto r = sample(1); r.payloadSha256 = hex64('B', 1); validation(r, "uppercase payload hash"); }
  { auto r = sample(1); r.target = "tc002"; validation(r, "production target"); }
  { auto r = sample(1); r.target = "experimental:"; validation(r, "bare prefix target"); }
  { auto r = sample(1); r.release = "1.0/1"; validation(r, "release with slash"); }
  { auto r = sample(1); r.release = std::string(65, 'r'); validation(r, "65-char release"); }
  { auto r = sample(1); r.stagedFile = ""; validation(r, "empty stagedFile"); }
  { auto r = sample(1); r.stagedFile = std::string("/a\0b", 4); validation(r, "NUL in stagedFile"); }
  { auto r = sample(1); r.stagedFile = std::string(4097, 'p'); validation(r, "4097-byte stagedFile"); }

  // The candidate is copied verbatim and survives a reload through a second instance.
  expectOk(state.stage("A", 1, sample(1)));
  update::Snapshot expected;
  expected.state = State::Staged;
  expected.lease = {"A", 100};
  expected.candidate.identity = release(1);
  expected.candidate.stagedFile = "/stage/1.awup";
  expected.candidate.fallback = false;
  CHECK(state.snapshot() == expected);
  update::UpdateState second(store);
  expectOk(second.load());
  CHECK(second.snapshot() == expected);

  // In-progress states refuse staging, including a replay of the identical package.
  const auto inProgress = [&](update::UpdateState& s) {
    const std::string bytes = store.json;
    const auto snapshot = s.snapshot();
    expectCode(s.stage("A", 2, sample(1)), "illegal-transition");
    expectCode(s.stage("A", 2, sample(2)), "illegal-transition");
    CHECK(s.snapshot() == snapshot);
    CHECK(store.json == bytes);
  };
  inProgress(state);
  expectOk(state.activate("A", 2, sample(1)));
  inProgress(state);
  expectOk(state.markBootPending("A", 2));
  inProgress(state);
  {
    MemoryStateStore quarantine;
    auto activating = expected;
    activating.state = State::Activating;
    auto s = loadFrom(quarantine, activating);
    CHECK(s.snapshot().state == State::Quarantined);
    const std::string bytes = quarantine.json;
    expectCode(s.stage("A", 2, sample(1)), "illegal-transition");
    expectCode(s.stage("A", 2, sample(2)), "illegal-transition");
    CHECK(quarantine.json == bytes);
    CHECK(s.snapshot().candidate == expected.candidate);
  }

  // Lease guards apply to stage, discard, activate and markBootPending alike.
  MemoryStateStore guardStore;
  auto stagedState = loadFrom(guardStore, expected);
  expectOk(stagedState.releaseLease("A", 3));
  {
    const std::string bytes = guardStore.json;
    expectCode(stagedState.stage("A", 3, sample(9)), "lease-required");
    expectCode(stagedState.discard("A", 3), "lease-required");
    expectCode(stagedState.activate("A", 3, sample(1)), "lease-required");
    expectCode(stagedState.markBootPending("A", 3), "lease-required");
    CHECK(guardStore.json == bytes);
    expectOk(stagedState.acquireLease("B", 3, 10));
    const std::string held = guardStore.json;
    expectCode(stagedState.stage("A", 4, sample(9)), "lease-held");
    expectCode(stagedState.discard("A", 4), "lease-held");
    expectCode(stagedState.activate("A", 4, sample(1)), "lease-held");
    expectCode(stagedState.markBootPending("A", 4), "lease-held");
    CHECK(guardStore.json == held);
    expectOk(stagedState.releaseLease("B", 4));
    expectOk(stagedState.acquireLease("A", 100, 10));
    const std::string expired = guardStore.json;
    expectCode(stagedState.stage("A", 110, sample(9)), "lease-expired");
    expectCode(stagedState.discard("A", 110), "lease-expired");
    expectCode(stagedState.activate("A", 110, sample(1)), "lease-expired");
    expectCode(stagedState.markBootPending("A", 110), "lease-expired");
    CHECK(guardStore.json == expired);
  }

  // Duplicate and downgrade against a confirmed release.
  MemoryStateStore confirmedStore;
  auto confirmed = loadFrom(confirmedStore, confirmedAt(5));
  expectCode(confirmed.stage("installer", 10, sample(5)), "duplicate");
  expectCode(confirmed.stage("installer", 10, sample(3)), "downgrade");
  expectCode(confirmed.stage("installer", 10, sample(4)), "downgrade");
  // A different build under the current counter is a duplicate as well, even
  // when a fallback authorization names it: the counter, not the hash, decides.
  auto conflicting = sample(5);
  conflicting.payloadSha256 = hex64('c', 5);
  expectCode(confirmed.stage("installer", 10, conflicting), "duplicate");
  const update::FallbackAuthorization conflictingAuthorization{5, hex64('c', 5)};
  expectCode(confirmed.stage("installer", 10, conflicting, &conflictingAuthorization), "duplicate");
  CHECK(confirmedStore.writes == 0);
  expectOk(confirmed.stage("installer", 10, sample(6)));
  CHECK(confirmed.snapshot().candidate.identity == release(6));
  CHECK(confirmed.snapshot().current == release(5));
  CHECK(confirmed.snapshot().acceptedCounter == 5);

  // A complete cycle: the confirmed counter becomes a duplicate, the next one stages.
  MemoryStateStore cycleStore;
  update::UpdateState cycle(cycleStore);
  expectOk(cycle.initialize(0));
  expectOk(cycle.acquireLease("installer", 0, 100));
  expectOk(cycle.stage("installer", 1, sample(1)));
  expectOk(cycle.activate("installer", 2, sample(1)));
  expectOk(cycle.markBootPending("installer", 3));
  expectOk(cycle.confirm());
  CHECK(cycle.snapshot().state == State::Confirmed);
  CHECK(cycle.snapshot().current == release(1));
  CHECK(cycle.snapshot().acceptedCounter == 1);
  CHECK(!cycle.snapshot().lease.held());
  expectOk(cycle.acquireLease("installer", 4, 100));
  expectCode(cycle.stage("installer", 5, sample(1)), "duplicate");
  expectOk(cycle.stage("installer", 5, sample(2)));
  CHECK(cycle.snapshot().candidate.identity == release(2));
}

void testFallback() {
  MemoryStateStore store;
  auto state = loadFrom(store, confirmedAt(5));
  const std::string bytes = store.json;
  const update::FallbackAuthorization wrongCounter{4, hex64('b', 3)};
  const update::FallbackAuthorization wrongHash{3, hex64('c', 3)};
  const update::FallbackAuthorization aboveAccepted{6, hex64('b', 6)};
  const update::FallbackAuthorization current{5, hex64('b', 5)};
  expectCode(state.stage("installer", 10, sample(3), &wrongCounter), "authorization-mismatch");
  expectCode(state.stage("installer", 10, sample(3), &wrongHash), "authorization-mismatch");
  expectCode(state.stage("installer", 10, sample(6), &aboveAccepted), "authorization-mismatch");
  expectCode(state.stage("installer", 10, sample(5), &current), "duplicate");
  CHECK(store.json == bytes);
  CHECK(store.writes == 0);

  const update::FallbackAuthorization three{3, hex64('b', 3)};
  expectOk(state.stage("installer", 10, sample(3), &three));
  CHECK(state.snapshot().state == State::Staged);
  CHECK(state.snapshot().candidate.fallback);
  CHECK(state.snapshot().candidate.identity == release(3));
  CHECK(state.snapshot().acceptedCounter == 5);
  update::Snapshot reloaded;
  std::string error;
  CHECK(update::parse(store.json, reloaded, error));
  CHECK(reloaded == state.snapshot());

  expectOk(state.activate("installer", 11, sample(3)));
  expectOk(state.markBootPending("installer", 12));
  expectOk(state.confirm());
  CHECK(state.snapshot().state == State::Confirmed);
  CHECK(state.snapshot().current == release(3));
  CHECK(state.snapshot().acceptedCounter == 5);
  CHECK(!state.snapshot().candidate.fallback);
  CHECK(state.snapshot().candidate.identity.counter == 0);

  expectOk(state.acquireLease("installer", 13, 100));
  expectCode(state.stage("installer", 14, sample(4)), "downgrade");
  expectCode(state.stage("installer", 14, sample(5)), "downgrade");
  expectCode(state.stage("installer", 14, sample(3)), "duplicate");
  const update::FallbackAuthorization four{4, hex64('b', 4)};
  expectOk(state.stage("installer", 14, sample(4), &four));
  CHECK(state.snapshot().candidate.fallback);
  expectOk(state.discard("installer", 15));
  CHECK(state.snapshot().state == State::Idle);
  CHECK(state.snapshot().current == release(3));
  // The previously confirmed high-water release stages again only with an
  // authorization naming it; the policy keeps no list of refused counters.
  const update::FallbackAuthorization five{5, hex64('b', 5)};
  expectOk(state.stage("installer", 15, sample(5), &five));
  CHECK(state.snapshot().candidate.fallback);
  CHECK(state.snapshot().candidate.identity == release(5));
  CHECK(state.snapshot().acceptedCounter == 5);
  CHECK(update::parse(store.json, reloaded, error));
  CHECK(reloaded == state.snapshot());
  expectOk(state.discard("installer", 15));
  CHECK(state.snapshot().current == release(3));
  expectOk(state.stage("installer", 16, sample(6)));
  CHECK(!state.snapshot().candidate.fallback);
  CHECK(state.snapshot().acceptedCounter == 5);
  const update::FallbackAuthorization six{6, hex64('b', 6)};
  expectOk(state.discard("installer", 17));
  expectCode(state.stage("installer", 17, sample(6), &six), "authorization-mismatch");

  // A counter equal to the accepted counter is at or below it: with no current
  // release it is refused as a downgrade and admitted by an authorization.
  MemoryStateStore unmanaged;
  update::UpdateState fresh(unmanaged);
  expectOk(fresh.initialize(5));
  expectOk(fresh.acquireLease("installer", 0, 100));
  expectCode(fresh.stage("installer", 1, sample(5)), "downgrade");
  expectCode(fresh.stage("installer", 1, sample(5), &four), "authorization-mismatch");
  CHECK(unmanaged.writes == 2);
  expectOk(fresh.stage("installer", 1, sample(5), &five));
  CHECK(fresh.snapshot().state == State::Staged);
  CHECK(fresh.snapshot().candidate.fallback);
  CHECK(fresh.snapshot().candidate.identity == release(5));
  CHECK(fresh.snapshot().acceptedCounter == 5);
  CHECK(fresh.snapshot().current.counter == 0);
  CHECK(update::parse(unmanaged.json, reloaded, error));
  CHECK(reloaded == fresh.snapshot());
  expectOk(fresh.activate("installer", 2, sample(5)));
  expectOk(fresh.markBootPending("installer", 3));
  expectOk(fresh.confirm());
  CHECK(fresh.snapshot().current == release(5));
  CHECK(fresh.snapshot().acceptedCounter == 5);
}

void testProductionTarget() {
  MemoryStateStore store;
  update::UpdateState state(store);
  expectOk(state.initialize(0));
  expectOk(state.acquireLease("daemon", 0, 100));
  auto production = sample(1712345678);
  production.target = update::kProductionTarget;
  expectOk(state.stage("daemon", 1, production));
  expectOk(state.activate("daemon", 2, production));
  expectOk(state.markBootPending("daemon", 3));
  expectOk(state.confirm());
  CHECK(state.snapshot().acceptedCounter == 1712345678);
  CHECK(state.snapshot().current.target == "awtrix-ng:tc002");
  update::Snapshot parsed;
  std::string error;
  CHECK(update::parse(store.json, parsed, error));
  CHECK(parsed == state.snapshot());
  expectOk(state.acquireLease("daemon", 4, 100));
  for (const char* target : {"awtrix-ng:tc001", "awtrix-ng:tc002 ", "AWTRIX-NG:TC002", "awtrix-ng"}) {
    auto other = sample(1712345679);
    other.target = target;
    expectCode(state.stage("daemon", 5, other), "invalid-input");
  }
  auto next = sample(1712345679);
  next.target = update::kProductionTarget;
  expectOk(state.stage("daemon", 5, next));
}

void testCounterMaximum() {
  MemoryStateStore store;
  update::UpdateState state(store);
  expectOk(state.initialize(UINT64_MAX - 1));
  expectOk(state.acquireLease("installer", 0, 100));
  expectCode(state.stage("installer", 1, sample(UINT64_MAX - 1)), "downgrade");
  expectOk(state.stage("installer", 1, sample(UINT64_MAX)));
  expectOk(state.activate("installer", 2, sample(UINT64_MAX)));
  expectOk(state.markBootPending("installer", 3));
  expectOk(state.confirm());
  CHECK(state.snapshot().acceptedCounter == UINT64_MAX);
  CHECK(state.snapshot().current.counter == UINT64_MAX);
  expectOk(state.acquireLease("installer", 4, 100));
  expectCode(state.stage("installer", 5, sample(1)), "counter-maximum");
  expectCode(state.stage("installer", 5, sample(UINT64_MAX - 1)), "counter-maximum");
  expectCode(state.stage("installer", 5, sample(UINT64_MAX)), "duplicate");
  const update::FallbackAuthorization seven{7, hex64('b', 7)};
  expectOk(state.stage("installer", 5, sample(7), &seven));
  CHECK(state.snapshot().candidate.fallback);
  CHECK(state.snapshot().acceptedCounter == UINT64_MAX);
  update::Snapshot reloaded;
  std::string error;
  CHECK(update::parse(store.json, reloaded, error));
  CHECK(reloaded == state.snapshot());
}

// Snapshot in `state` with current release 5 and, where the state carries one, candidate 6.
update::Snapshot snapshotFor(State state) {
  auto s = confirmedAt(5);
  s.state = state;
  if (state == State::Staged || state == State::Activating || state == State::BootPending ||
      state == State::Quarantined) {
    s.candidate.identity = release(6);
    s.candidate.stagedFile = "/stage/6.awup";
  }
  if (state == State::Quarantined) s.failure = "activation interrupted";
  if (state == State::RolledBack) s.failure = "boot failed";
  return s;
}

// Loads `state` into memory; activating cannot be loaded (it reconciles), so it is reached
// through activate() from staged.
update::UpdateState enter(MemoryStateStore& store, State state) {
  if (state != State::Activating) return loadFrom(store, snapshotFor(state));
  auto s = loadFrom(store, snapshotFor(State::Staged));
  expectOk(s.activate("installer", 10, sample(6)));
  CHECK(s.snapshot() == snapshotFor(State::Activating));
  store.writes = 0;
  return s;
}

void testTransitionMatrix() {
  const State all[] = {State::Idle, State::Staged, State::Activating, State::BootPending,
                       State::Confirmed, State::RolledBack, State::Quarantined};
  struct Transition { const char* name; std::vector<State> sources; };
  const Transition transitions[] = {
      {"discard", {State::Staged}},
      {"activate", {State::Staged}},
      {"markBootPending", {State::Activating}},
      {"confirm", {State::BootPending}},
      {"rollback", {State::Activating, State::BootPending, State::Quarantined}},
      {"stage", {State::Idle, State::Confirmed, State::RolledBack}},
  };
  const auto run = [](update::UpdateState& s, const char* name) {
    for (const auto& op : operations("installer", 10))
      if (std::string(op.name) == name) return op.run(s);
    throw std::runtime_error("unknown operation");
  };
  for (const State from : all) {
    for (const auto& t : transitions) {
      MemoryStateStore store;
      auto s = enter(store, from);
      const auto before = s.snapshot();
      const bool legal = std::find(t.sources.begin(), t.sources.end(), from) != t.sources.end();
      const std::string name = t.name;
      auto o = name == "stage" ? s.stage("installer", 10, sample(7))
             : name == "activate" ? s.activate("installer", 10, sample(6)) : run(s, t.name);
      if (!legal) {
        expectCode(o, "illegal-transition");
        CHECK(s.snapshot() == before);
        CHECK(store.writes == 0);
        continue;
      }
      expectOk(o);
      CHECK(store.writes == 1);
      const auto& after = s.snapshot();
      if (std::string(t.name) == "confirm") {
        CHECK(after.state == State::Confirmed);
        CHECK(after.acceptedCounter == 6);
        CHECK(after.current == release(6));
        CHECK(after.candidate.identity.counter == 0);
        CHECK(after.candidate.stagedFile.empty());
        CHECK(!after.lease.held());
        CHECK(after.failure.empty());
      } else {
        CHECK(after.acceptedCounter == before.acceptedCounter);
        CHECK(after.current == release(5));
      }
      if (std::string(t.name) == "rollback") {
        CHECK(after.state == State::RolledBack);
        CHECK(after.failure == "boot failed");
        CHECK(after.candidate.identity.counter == 0);
        CHECK(!after.lease.held());
      }
      if (std::string(t.name) == "discard") {
        CHECK(after.state == State::Idle);
        CHECK(after.candidate.identity.counter == 0);
        CHECK(after.lease == before.lease);
      }
      if (std::string(t.name) == "activate") CHECK(after.state == State::Activating);
      if (std::string(t.name) == "markBootPending") CHECK(after.state == State::BootPending);
      if (std::string(t.name) == "stage") {
        CHECK(after.state == State::Staged);
        CHECK(after.candidate.identity == release(7));
        CHECK(after.failure.empty());
      }
      update::Snapshot reloaded;
      std::string error;
      CHECK(update::parse(store.json, reloaded, error));
      CHECK(reloaded == after);
    }
  }

  // activate needs a re-verified Result equal to the candidate.
  for (int field = 0; field < 5; ++field) {
    MemoryStateStore store;
    auto s = enter(store, State::Staged);
    auto r = sample(6);
    switch (field) {
      case 0: r.ok = false; break;
      case 1: r.counter = 7; break;
      case 2: r.payloadSha256 = hex64('b', 7); break;
      case 3: r.release = "1.0.7"; break;
      case 4: r.target = "experimental:other"; break;
    }
    expectCode(s.activate("installer", 10, r), "invalid-input");
    CHECK(s.snapshot().state == State::Staged);
    CHECK(store.writes == 0);
  }
  {
    MemoryStateStore store;
    auto s = enter(store, State::Staged);
    auto r = sample(6);
    r.stagedFile = "/elsewhere/6.awup";
    r.payloadBytes = 99;
    expectOk(s.activate("installer", 10, r));
    CHECK(s.snapshot().candidate.stagedFile == "/stage/6.awup");
  }

  // rollback reason limits.
  for (const std::string& reason : {std::string(), std::string(257, 'r'), std::string("tab\there"),
                                   std::string("del\x7f"), std::string("umlaut\xc3\xa4")}) {
    MemoryStateStore store;
    auto s = enter(store, State::BootPending);
    expectCode(s.rollback(reason), "invalid-input");
    CHECK(s.snapshot().state == State::BootPending);
    CHECK(store.writes == 0);
  }
  {
    MemoryStateStore store;
    auto s = enter(store, State::BootPending);
    expectOk(s.rollback(std::string(256, 'r')));
    CHECK(s.snapshot().failure == std::string(256, 'r'));
  }
}

void testReconcileOnLoad() {
  MemoryStateStore store;
  auto s = loadFrom(store, snapshotFor(State::Activating));
  CHECK(s.snapshot().state == State::Quarantined);
  CHECK(s.snapshot().failure == "activation interrupted");
  CHECK(s.snapshot().candidate == snapshotFor(State::Activating).candidate);
  CHECK(store.writes == 0);
  CHECK(store.json == update::serialize(snapshotFor(State::Activating)));
  expectCode(s.stage("installer", 10, sample(7)), "illegal-transition");
  expectCode(s.discard("installer", 10), "illegal-transition");
  expectCode(s.activate("installer", 10, sample(6)), "illegal-transition");
  expectCode(s.markBootPending("installer", 10), "illegal-transition");
  expectCode(s.confirm(), "illegal-transition");
  CHECK(store.writes == 0);
  expectOk(s.releaseLease("installer", 10));
  expectOk(s.acquireLease("operator", 10, 60));
  CHECK(store.writes == 2);
  update::Snapshot persisted;
  std::string error;
  CHECK(update::parse(store.json, persisted, error));
  CHECK(persisted.state == State::Quarantined);
  CHECK(persisted.failure == "activation interrupted");
  expectOk(s.rollback("candidate kernel did not boot"));
  CHECK(s.snapshot().state == State::RolledBack);
  CHECK(s.snapshot().current == release(5));
  CHECK(s.snapshot().acceptedCounter == 5);
  CHECK(update::parse(store.json, persisted, error));
  CHECK(persisted == s.snapshot());
  CHECK(!persisted.lease.held());

  for (const State state : {State::Idle, State::Staged, State::BootPending, State::Confirmed,
                            State::RolledBack, State::Quarantined}) {
    MemoryStateStore unchanged;
    auto loaded = loadFrom(unchanged, snapshotFor(state));
    CHECK(loaded.snapshot() == snapshotFor(state));
    CHECK(unchanged.writes == 0);
  }
}

void testMemoryWriteFailure() {
  MemoryStateStore store;
  update::UpdateState state(store);
  expectOk(state.initialize(0));
  expectOk(state.acquireLease("installer", 0, 100));
  const auto check = [&](const char* label) {
    const auto before = state.snapshot();
    const std::string bytes = store.json;
    store.failWrite = true;
    update::Outcome o;
    if (std::string(label) == "stage") o = state.stage("installer", 1, sample(1));
    else if (std::string(label) == "confirm") o = state.confirm();
    else o = state.acquireLease("installer", 1, 50);
    store.failWrite = false;
    expectCode(o, "storage");
    CHECK(!state.loaded());
    CHECK(state.snapshot() == before);
    CHECK(store.json == bytes);
    const int writes = store.writes;
    for (const auto& op : operations("installer", 1)) expectCode(op.run(state), "not-loaded");
    CHECK(store.writes == writes);
    CHECK(store.json == bytes);
    update::UpdateState fresh(store);
    expectOk(fresh.load());
    CHECK(fresh.snapshot() == before);
    expectOk(state.load());
    CHECK(state.loaded());
    CHECK(state.snapshot() == before);
  };
  check("acquireLease");
  check("stage");
  expectOk(state.stage("installer", 2, sample(1)));
  expectOk(state.activate("installer", 3, sample(1)));
  expectOk(state.markBootPending("installer", 4));
  check("confirm");
  CHECK(state.snapshot().acceptedCounter == 0);
  expectOk(state.confirm());
  CHECK(state.snapshot().acceptedCounter == 1);
  CHECK(!state.snapshot().lease.held());
  check("acquireLease");
  update::UpdateState fresh(store);
  expectOk(fresh.load());
  CHECK(!fresh.snapshot().lease.held());
  CHECK(fresh.snapshot().state == State::Confirmed);

  // initialize() on a loaded object whose file vanished: a failed write drops the loaded state.
  MemoryStateStore vanished;
  update::UpdateState reinit(vanished);
  expectOk(reinit.initialize(0));
  vanished.exists = false;
  vanished.failWrite = true;
  expectCode(reinit.initialize(1), "storage");
  CHECK(!reinit.loaded());
  expectCode(reinit.acquireLease("installer", 1, 50), "not-loaded");
  vanished.failWrite = false;
  expectOk(reinit.initialize(1));
  CHECK(reinit.loaded());
  CHECK(reinit.snapshot().acceptedCounter == 1);

  // initialize() on a loaded object whose store cannot be read: a failed read drops it too.
  MemoryStateStore unreadable;
  update::UpdateState reread(unreadable);
  expectOk(reread.initialize(4));
  const auto known = reread.snapshot();
  const std::string knownBytes = unreadable.json;
  unreadable.failRead = true;
  expectCode(reread.initialize(5), "storage");
  CHECK(!reread.loaded());
  CHECK(reread.snapshot() == known);
  for (const auto& op : operations("installer", 1)) expectCode(op.run(reread), "not-loaded");
  CHECK(unreadable.json == knownBytes);
  CHECK(unreadable.writes == 1);
  unreadable.failRead = false;
  expectOk(reread.load());
  CHECK(reread.snapshot() == known);

  // A mutating operation reads the document before it writes; a failed read writes nothing.
  MemoryStateStore rereadBeforeWrite;
  auto staging = loadFrom(rereadBeforeWrite, confirmedAt(5));
  const auto view = staging.snapshot();
  rereadBeforeWrite.failRead = true;
  expectCode(staging.stage("installer", 10, sample(6)), "storage");
  rereadBeforeWrite.failRead = false;
  CHECK(!staging.loaded());
  CHECK(staging.snapshot() == view);
  CHECK(rereadBeforeWrite.writes == 0);
  CHECK(rereadBeforeWrite.json == update::serialize(confirmedAt(5)));
  for (const auto& op : operations("installer", 10)) expectCode(op.run(staging), "not-loaded");
  CHECK(rereadBeforeWrite.writes == 0);
  expectOk(staging.load());
  expectOk(staging.stage("installer", 10, sample(6)));
}

// Runs two confirmed cycles, for counters 1 and 2, through `state`.
void confirmTwoReleases(update::UpdateState& state) {
  for (std::uint64_t counter = 1; counter <= 2; ++counter) {
    const std::uint64_t now = counter * 10;
    expectOk(state.acquireLease("installer", now, 100));
    expectOk(state.stage("installer", now + 1, sample(counter)));
    expectOk(state.activate("installer", now + 2, sample(counter)));
    expectOk(state.markBootPending("installer", now + 3));
    expectOk(state.confirm());
  }
  CHECK(state.snapshot().acceptedCounter == 2);
  CHECK(state.snapshot().current == release(2));
}

// Several objects on one store: a write succeeds only while the store holds exactly the
// document the writing object last read or wrote.
void testStaleObject() {
  {
    // An object loaded before two confirmed cycles must not put the accepted counter back.
    MemoryStateStore store;
    update::UpdateState current(store);
    expectOk(current.initialize(0));
    update::UpdateState stale(store);
    expectOk(stale.load());
    confirmTwoReleases(current);
    const std::string confirmed = store.json;
    const int writes = store.writes;
    const auto view = stale.snapshot();
    expectCode(stale.acquireLease("installer", 30, 100), "stale");
    CHECK(!stale.loaded());
    CHECK(stale.snapshot() == view);
    CHECK(store.json == confirmed);
    CHECK(store.writes == writes);
    for (const auto& op : operations("installer", 31)) expectCode(op.run(stale), "not-loaded");
    CHECK(store.json == confirmed);
    CHECK(store.writes == writes);

    // load() makes the object valid again, and release 1 still needs an authorization.
    expectOk(stale.load());
    CHECK(stale.loaded());
    CHECK(stale.snapshot() == current.snapshot());
    expectOk(stale.acquireLease("installer", 30, 100));
    expectCode(stale.stage("installer", 31, sample(1)), "downgrade");
    const update::FallbackAuthorization one{1, hex64('b', 1)};
    expectOk(stale.stage("installer", 31, sample(1), &one));
    CHECK(stale.snapshot().candidate.fallback);
    CHECK(stale.snapshot().acceptedCounter == 2);

    // The object that confirmed is now the stale one.
    const std::string staged = store.json;
    expectCode(current.acquireLease("installer", 32, 100), "stale");
    CHECK(!current.loaded());
    CHECK(store.json == staged);
    update::Snapshot persisted;
    std::string error;
    CHECK(update::parse(store.json, persisted, error));
    CHECK(persisted == stale.snapshot());
  }

  // Every mutating operation, started where it is legal, refuses after each kind of change.
  struct Case {
    const char* name;
    State from;
    std::function<update::Outcome(update::UpdateState&)> run;
  };
  const std::vector<Case> cases = {
      {"acquireLease", State::Confirmed, [](update::UpdateState& s) { return s.acquireLease("installer", 10, 60); }},
      {"releaseLease", State::Confirmed, [](update::UpdateState& s) { return s.releaseLease("installer", 10); }},
      {"stage", State::Confirmed, [](update::UpdateState& s) { return s.stage("installer", 10, sample(7)); }},
      {"discard", State::Staged, [](update::UpdateState& s) { return s.discard("installer", 10); }},
      {"activate", State::Staged, [](update::UpdateState& s) { return s.activate("installer", 10, sample(6)); }},
      {"markBootPending", State::Activating, [](update::UpdateState& s) { return s.markBootPending("installer", 10); }},
      {"confirm", State::BootPending, [](update::UpdateState& s) { return s.confirm(); }},
      {"rollback", State::Quarantined, [](update::UpdateState& s) { return s.rollback("boot failed"); }},
  };
  enum Change { OtherObjectWrote, Removed, Reinitialized, SameMeaningOtherBytes };
  for (const auto& c : cases) {
    {
      MemoryStateStore control;
      auto legal = enter(control, c.from);
      expectOk(c.run(legal));
    }
    for (const Change change : {OtherObjectWrote, Removed, Reinitialized, SameMeaningOtherBytes}) {
      MemoryStateStore store;
      auto stale = enter(store, c.from);
      const auto view = stale.snapshot();
      if (change == OtherObjectWrote) {
        update::UpdateState other(store);
        expectOk(other.load());
        expectOk(other.acquireLease("installer", 10, 500));
      } else if (change == Removed) {
        store.exists = false;
        store.json.clear();
      } else if (change == Reinitialized) {
        store.exists = false;
        update::UpdateState other(store);
        expectOk(other.initialize(view.acceptedCounter));
      } else {
        store.json = " " + store.json + "\n";
      }
      const std::string bytes = store.json;
      const bool present = store.exists;
      const int writes = store.writes;
      const auto outcome = c.run(stale);
      if (outcome.ok || outcome.code != "stale" || outcome.error.empty())
        throw std::runtime_error(std::string(c.name) + " from a stale object after change " +
                                 std::to_string(change) + ": " + (outcome.ok ? "ok" : outcome.code));
      CHECK(!stale.loaded());
      CHECK(stale.snapshot() == view);
      CHECK(store.json == bytes);
      CHECK(store.exists == present);
      CHECK(store.writes == writes);
      for (const auto& op : operations("installer", 10)) expectCode(op.run(stale), "not-loaded");
      CHECK(store.json == bytes);
      CHECK(store.writes == writes);
      if (change == Removed) {
        expectCode(stale.load(), "uninitialized");
        continue;
      }
      expectOk(stale.load());
      update::UpdateState reader(store);
      expectOk(reader.load());
      CHECK(stale.snapshot() == reader.snapshot());
      if (change != SameMeaningOtherBytes) continue;
      // The same document in other bytes loads to the same view; the stored activation
      // reconciles to quarantined, where markBootPending is illegal.
      if (c.from == State::Activating) {
        expectCode(c.run(stale), "illegal-transition");
        CHECK(store.writes == writes);
      } else {
        expectOk(c.run(stale));
        CHECK(store.writes == writes + 1);
      }
    }
  }

  // The comparison is by content: a document written back byte for byte is the one the
  // object read, and the view it holds is the durable state.
  {
    MemoryStateStore store;
    auto first = loadFrom(store, confirmedAt(5));
    update::UpdateState second(store);
    expectOk(second.load());
    expectOk(second.acquireLease("installer", 10, 500));
    expectOk(second.acquireLease("installer", 900, 100));
    CHECK(store.json == update::serialize(confirmedAt(5)));
    expectOk(first.stage("installer", 10, sample(6)));
    expectCode(second.acquireLease("installer", 11, 100), "stale");
  }
}

std::string readBytes(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  CHECK(in.good());
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void writeBytes(const std::string& path, const std::string& bytes, mode_t mode = 0600) {
  const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode);
  CHECK(fd >= 0);
  CHECK(::write(fd, bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size()));
  CHECK(::fchmod(fd, mode) == 0);
  CHECK(::close(fd) == 0);
}

bool exists(const std::string& path) {
  struct stat status {};
  return ::lstat(path.c_str(), &status) == 0;
}

mode_t modeOf(const std::string& path) {
  struct stat status {};
  CHECK(::lstat(path.c_str(), &status) == 0);
  return status.st_mode & 07777;
}

void refusesDirectory(const std::string& directory) {
  update::FileStateStore store(directory);
  CHECK(!store.ok());
  CHECK(!store.error().empty());
  std::string json, error;
  bool present = true;
  CHECK(!store.read(json, present, error));
  CHECK(!error.empty());
  error.clear();
  CHECK(!store.write("{}", error));
  CHECK(!error.empty());
}

void testFileStoreBasics() {
  TempDir base;
  refusesDirectory(base.path() + "/missing");
  CHECK(::mkdir((base.path() + "/group").c_str(), 0750) == 0);
  refusesDirectory(base.path() + "/group");
  CHECK(::mkdir((base.path() + "/private").c_str(), 0700) == 0);
  CHECK(::symlink("private", (base.path() + "/link").c_str()) == 0);
  refusesDirectory(base.path() + "/link");
  // O_NOFOLLOW guards the last component only, so a symlink must not be reachable
  // through a trailing slash or a final "." or "..".
  refusesDirectory(base.path() + "/link/");
  refusesDirectory(base.path() + "/link//");
  refusesDirectory(base.path() + "/link/.");
  refusesDirectory(base.path() + "/link/./");
  refusesDirectory(base.path() + "/private/.");
  refusesDirectory(base.path() + "/private/..");
  refusesDirectory(base.path() + "/private/../private/.");
  refusesDirectory("/");
  refusesDirectory("///");
  refusesDirectory(".");
  refusesDirectory("..");
  writeBytes(base.path() + "/file", "x");
  refusesDirectory(base.path() + "/file");
  refusesDirectory("");
  std::string embeddedNul = base.path() + "/private";
  embeddedNul.push_back('\0');
  refusesDirectory(embeddedNul + "x");

  const std::string dir = base.path() + "/private";
  const std::string file = dir + "/update-state.json";
  {
    // A real directory named with trailing slashes is the same directory.
    update::FileStateStore slashed(dir + "//");
    CHECK(slashed.ok());
    CHECK(slashed.error().empty());
    update::FileStateStore busy(dir);
    CHECK(!busy.ok());
    CHECK(busy.error().find("in use") != std::string::npos);
  }
  {
    update::FileStateStore store(dir);
    CHECK(store.ok());
    CHECK(store.error().empty());
    std::string json = "stale", error;
    bool present = true;
    CHECK(store.read(json, present, error));
    CHECK(!present);
    CHECK(json.empty());
    CHECK(error.empty());
    update::Snapshot idle;
    idle.acceptedCounter = 3;
    CHECK(store.write(update::serialize(idle), error));
    CHECK(error.empty());
    CHECK(modeOf(file) == 0600);
    CHECK(!exists(dir + "/.update-state.tmp"));
    CHECK(store.read(json, present, error));
    CHECK(present);
    CHECK(json == update::serialize(idle));
    CHECK(readBytes(file) == update::serialize(idle));

    // A second instance is refused while the first holds the directory.
    update::FileStateStore second(dir);
    CHECK(!second.ok());
    CHECK(second.error().find("in use") != std::string::npos);
    CHECK(!second.read(json, present, error));
  }
  {
    update::FileStateStore third(dir);
    CHECK(third.ok());
    std::string json, error;
    bool present = false;
    CHECK(third.read(json, present, error));
    CHECK(present);
  }

  // Unsafe state files are refused and never modified.
  const auto refusesFile = [&](const std::string& label) {
    update::FileStateStore store(dir);
    CHECK(store.ok());
    struct stat prior {};
    CHECK(::lstat(file.c_str(), &prior) == 0);
    const bool regular = S_ISREG(prior.st_mode);
    const std::string before = regular ? readBytes(file) : std::string();
    std::string json, error;
    bool present = true;
    if (store.read(json, present, error) || error.empty())
      throw std::runtime_error("read accepted " + label);
    struct stat later {};
    CHECK(::lstat(file.c_str(), &later) == 0);
    CHECK(prior.st_mode == later.st_mode && prior.st_size == later.st_size && prior.st_ino == later.st_ino);
    if (regular) CHECK(readBytes(file) == before);
  };
  CHECK(::unlink(file.c_str()) == 0);
  writeBytes(dir + "/victim.json", "{\"schema\":2}");
  CHECK(::symlink("victim.json", file.c_str()) == 0);
  refusesFile("symlinked state file");
  CHECK(readBytes(dir + "/victim.json") == "{\"schema\":2}");
  CHECK(::unlink(file.c_str()) == 0);
  // Any group or other permission bit, not only read, makes the file unsafe.
  for (const mode_t mode : {0640, 0620, 0610, 0604, 0602, 0601}) {
    writeBytes(file, "{\"schema\":2}", mode);
    refusesFile("state file with mode " + std::to_string(mode));
    CHECK(::unlink(file.c_str()) == 0);
  }
  writeBytes(file, std::string(update::kMaxStateBytes + 1, ' '));
  refusesFile("oversized state file");
  CHECK(::unlink(file.c_str()) == 0);
  CHECK(::mkdir(file.c_str(), 0700) == 0);
  refusesFile("directory named like the state file");
  CHECK(::rmdir(file.c_str()) == 0);
  writeBytes(file, std::string(update::kMaxStateBytes, ' '));
  {
    update::FileStateStore store(dir);
    std::string json, error;
    bool present = false;
    CHECK(store.read(json, present, error));
    CHECK(present);
    CHECK(json.size() == update::kMaxStateBytes);
  }
}

void testFileStoreFaults() {
  TempDir base;
  const std::string dir = base.path();
  const std::string file = dir + "/update-state.json";
  const std::string temp = dir + "/.update-state.tmp";
  std::string faultStep;
  const auto fault = [&](const char* step) { return faultStep == step; };

  update::Snapshot pre;
  pre.lease = {"installer", 100};
  {
    update::FileStateStore store(dir, fault);
    CHECK(store.ok());
    update::UpdateState s(store);
    expectOk(s.initialize(0));
    expectOk(s.acquireLease("installer", 0, 100));
    CHECK(s.snapshot() == pre);
  }
  update::Snapshot post = pre;
  post.state = State::Staged;
  post.candidate.identity = release(1);
  post.candidate.stagedFile = "/stage/1.awup";

  // A fault at any step, including the read before the write, yields the pre-op document;
  // only a failed directory sync leaves the published post-op document (with the uncertainty named).
  for (const char* step : {"read", "unlink-stale", "open", "write", "fsync-file", "close", "rename", "fsync-directory"}) {
    const bool published = std::string(step) == "fsync-directory";
    {
      update::FileStateStore store(dir, fault);
      CHECK(store.ok());
      update::UpdateState s(store);
      expectOk(s.load());
      CHECK(s.snapshot() == pre);
      faultStep = step;
      const auto outcome = s.stage("installer", 1, sample(1));
      faultStep.clear();
      expectCode(outcome, "storage");
      CHECK((outcome.error.find("uncertain") != std::string::npos) == published);
      CHECK(!s.loaded());
      CHECK(s.snapshot() == pre);
      CHECK(!exists(temp));
      CHECK(readBytes(file) == update::serialize(published ? post : pre));
      // Nothing runs from the stale snapshot until load() re-reads the visible document.
      for (const auto& op : operations("installer", 2)) expectCode(op.run(s), "not-loaded");
      CHECK(readBytes(file) == update::serialize(published ? post : pre));
      CHECK(!exists(temp));
      expectOk(s.load());
      CHECK(s.loaded());
      CHECK(s.snapshot() == (published ? post : pre));
      expectOk(s.acquireLease("installer", 2, 100));
      CHECK(readBytes(file) == update::serialize(s.snapshot()));
      CHECK(!exists(temp));
    }
    {
      update::FileStateStore fresh(dir);
      CHECK(fresh.ok());
      update::UpdateState s(fresh);
      expectOk(s.load());
      auto expected = published ? post : pre;
      expected.lease = {"installer", 102};
      CHECK(s.snapshot() == expected);
      if (published) expectOk(s.discard("installer", 3));
      expectOk(s.acquireLease("installer", 0, 100));
      CHECK(s.snapshot() == pre);
    }
  }

  // A confirm whose directory sync fails has published the accepted counter; the
  // stale boot-pending view must not roll it back on disk.
  {
    TempDir uncertain;
    const std::string stateFile = uncertain.path() + "/update-state.json";
    update::FileStateStore store(uncertain.path(), fault);
    CHECK(store.ok());
    update::UpdateState s(store);
    expectOk(s.initialize(5));
    expectOk(s.acquireLease("installer", 0, 100));
    expectOk(s.stage("installer", 1, sample(6)));
    expectOk(s.activate("installer", 2, sample(6)));
    expectOk(s.markBootPending("installer", 3));
    const auto bootPending = s.snapshot();
    faultStep = "fsync-directory";
    const auto outcome = s.confirm();
    faultStep.clear();
    expectCode(outcome, "storage");
    CHECK(outcome.error.find("uncertain") != std::string::npos);
    CHECK(!s.loaded());
    CHECK(s.snapshot() == bootPending);
    update::Snapshot confirmed;
    confirmed.state = State::Confirmed;
    confirmed.acceptedCounter = 6;
    confirmed.current = release(6);
    CHECK(readBytes(stateFile) == update::serialize(confirmed));
    expectCode(s.rollback("boot failed"), "not-loaded");
    expectCode(s.acquireLease("installer", 4, 100), "not-loaded");
    expectCode(s.confirm(), "not-loaded");
    CHECK(readBytes(stateFile) == update::serialize(confirmed));
    expectOk(s.load());
    CHECK(s.snapshot() == confirmed);
    expectCode(s.rollback("boot failed"), "illegal-transition");
    CHECK(readBytes(stateFile) == update::serialize(confirmed));
    expectOk(s.acquireLease("installer", 4, 100));
    CHECK(s.snapshot().acceptedCounter == 6);
    CHECK(s.snapshot().current == release(6));
  }

  // A read fault fails load() and initialize() without touching the file.
  {
    update::FileStateStore store(dir, fault);
    update::UpdateState s(store);
    faultStep = "read";
    expectCode(s.load(), "storage");
    CHECK(!s.loaded());
    expectCode(s.initialize(3), "storage");
    faultStep.clear();
    CHECK(readBytes(file) == update::serialize(pre));
    expectOk(s.load());
    CHECK(s.snapshot() == pre);
    faultStep = "read";
    expectCode(s.initialize(3), "storage");
    faultStep.clear();
    CHECK(!s.loaded());
    CHECK(s.snapshot() == pre);
    expectCode(s.acquireLease("installer", 1, 10), "not-loaded");
    CHECK(readBytes(file) == update::serialize(pre));
  }

  // Leftover temporaries are ignored by load() and replaced by the next write.
  {
    update::Snapshot other;
    other.acceptedCounter = 42;
    writeBytes(temp, update::serialize(other));
    update::FileStateStore store(dir);
    update::UpdateState s(store);
    expectOk(s.load());
    CHECK(s.snapshot() == pre);
    expectOk(s.acquireLease("installer", 5, 100));
    CHECK(!exists(temp));
    CHECK(readBytes(file) == update::serialize(s.snapshot()));
    expectOk(s.acquireLease("installer", 0, 100));
  }
  {
    writeBytes(dir + "/victim", "victim bytes");
    CHECK(::symlink("victim", temp.c_str()) == 0);
    update::FileStateStore store(dir);
    update::UpdateState s(store);
    expectOk(s.load());
    CHECK(s.snapshot() == pre);
    expectOk(s.acquireLease("installer", 6, 100));
    CHECK(readBytes(dir + "/victim") == "victim bytes");
    CHECK(!exists(temp));
    CHECK(modeOf(file) == 0600);
    CHECK(readBytes(file) == update::serialize(s.snapshot()));
  }

  // The full cycle, reloaded through a fresh store after every durable transition.
  TempDir cycle;
  const auto reload = [&](const update::Snapshot& expected) {
    update::FileStateStore fresh(cycle.path());
    CHECK(fresh.ok());
    update::UpdateState s(fresh);
    expectOk(s.load());
    CHECK(s.snapshot() == expected);
    CHECK(readBytes(cycle.path() + "/update-state.json") == update::serialize(expected));
  };
  update::Snapshot expected;
  {
    update::FileStateStore store(cycle.path());
    update::UpdateState s(store);
    expectOk(s.initialize(0));
    expected = s.snapshot();
  }
  reload(expected);
  {
    update::FileStateStore store(cycle.path());
    update::UpdateState s(store);
    expectOk(s.load());
    expectOk(s.acquireLease("installer", 10, 60));
    expected = s.snapshot();
  }
  reload(expected);
  {
    update::FileStateStore store(cycle.path());
    update::UpdateState s(store);
    expectOk(s.load());
    expectOk(s.stage("installer", 11, sample(1)));
    expected = s.snapshot();
  }
  reload(expected);
  {
    update::FileStateStore store(cycle.path());
    update::UpdateState s(store);
    expectOk(s.load());
    expectOk(s.activate("installer", 12, sample(1)));
    CHECK(s.snapshot().state == State::Activating);
    expected = s.snapshot();
  }
  {
    // An activation left on disk reconciles to quarantined without a write.
    auto quarantined = expected;
    quarantined.state = State::Quarantined;
    quarantined.failure = "activation interrupted";
    update::FileStateStore fresh(cycle.path());
    update::UpdateState s(fresh);
    expectOk(s.load());
    CHECK(s.snapshot() == quarantined);
    CHECK(readBytes(cycle.path() + "/update-state.json") == update::serialize(expected));
    expectCode(s.markBootPending("installer", 13), "illegal-transition");
    expectOk(s.rollback("power lost during activation"));
    expected = s.snapshot();
  }
  reload(expected);
  {
    update::FileStateStore store(cycle.path());
    update::UpdateState s(store);
    expectOk(s.load());
    CHECK(s.snapshot().state == State::RolledBack);
    expectOk(s.acquireLease("installer", 14, 60));
    expectOk(s.stage("installer", 15, sample(1)));
    expectOk(s.activate("installer", 16, sample(1)));
    expectOk(s.markBootPending("installer", 17));
    expected = s.snapshot();
  }
  reload(expected);
  {
    update::FileStateStore store(cycle.path());
    update::UpdateState s(store);
    expectOk(s.load());
    expectOk(s.confirm());
    expected = s.snapshot();
    CHECK(expected.state == State::Confirmed);
    CHECK(expected.acceptedCounter == 1);
    CHECK(expected.current == release(1));
    CHECK(!expected.lease.held());
  }
  reload(expected);
}

// Two objects on one FileStateStore: the directory lock admits both, and the document
// comparison refuses the one whose view is stale.
void testFileStoreSharedObjects() {
  TempDir base;
  const std::string file = base.path() + "/update-state.json";
  const std::string temp = base.path() + "/.update-state.tmp";
  {
    update::FileStateStore store(base.path());
    CHECK(store.ok());
    update::UpdateState current(store);
    expectOk(current.initialize(0));
    update::UpdateState stale(store);
    expectOk(stale.load());
    confirmTwoReleases(current);
    const std::string confirmed = readBytes(file);
    CHECK(confirmed == update::serialize(current.snapshot()));

    expectCode(stale.acquireLease("installer", 30, 100), "stale");
    CHECK(!stale.loaded());
    CHECK(stale.snapshot().acceptedCounter == 0);
    CHECK(readBytes(file) == confirmed);
    CHECK(!exists(temp));
    for (const auto& op : operations("installer", 31)) expectCode(op.run(stale), "not-loaded");
    CHECK(readBytes(file) == confirmed);

    expectOk(stale.load());
    CHECK(stale.snapshot() == current.snapshot());
    expectOk(stale.acquireLease("installer", 30, 100));
    expectCode(stale.stage("installer", 31, sample(1)), "downgrade");
    const std::string leased = readBytes(file);
    CHECK(leased == update::serialize(stale.snapshot()));
    expectCode(current.acquireLease("installer", 32, 100), "stale");
    CHECK(!current.loaded());
    CHECK(readBytes(file) == leased);

    // A document replaced behind the store, or removed, is a change as well.
    writeBytes(file, " " + leased);
    expectCode(stale.stage("installer", 31, sample(3)), "stale");
    CHECK(readBytes(file) == " " + leased);
    expectOk(stale.load());
    expectOk(stale.stage("installer", 31, sample(3)));
    CHECK(::unlink(file.c_str()) == 0);
    expectCode(stale.discard("installer", 32), "stale");
    CHECK(!exists(file));
    CHECK(!exists(temp));
    expectCode(stale.load(), "uninitialized");
    writeBytes(file, confirmed);
  }
  update::FileStateStore fresh(base.path());
  CHECK(fresh.ok());
  update::UpdateState later(fresh);
  expectOk(later.load());
  CHECK(later.snapshot().acceptedCounter == 2);
  CHECK(later.snapshot().current == release(2));
  expectOk(later.acquireLease("installer", 40, 100));
  expectCode(later.stage("installer", 41, sample(1)), "downgrade");
}

}  // namespace

int main(int argc, char** argv) {
  try {
    CHECK(argc == 2);
    testSharedTargetVectors(argv[1]);
    testStateNames();
    testCodecRoundTrip();
    testCodecRejections();
    testLegacyStateMigration();
    testInitialize();
    testLease();
    testStageGuards();
    testFallback();
    testProductionTarget();
    testCounterMaximum();
    testTransitionMatrix();
    testReconcileOnLoad();
    testMemoryWriteFailure();
    testStaleObject();
    testFileStoreBasics();
    testFileStoreFaults();
    testFileStoreSharedObjects();
  } catch (const std::exception& error) {
    std::cerr << "update-state-test: " << error.what() << "\n";
    return 1;
  }
  return 0;
}
