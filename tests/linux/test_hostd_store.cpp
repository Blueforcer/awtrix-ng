#include "../support.h"

#include <filesystem>
#include <string>

#include "platform/linux/hostd/ClockStore.h"
#include "platform/linux/hostd/PairingCode.h"
#include "platform/host/Files.h"

using awtrix::hostd::ClockRecord;
using awtrix::hostd::ClockStore;
using awtrix::hostd::PairingCode;
using awtrix::test::check;

namespace fs = std::filesystem;

namespace {

fs::path freshRoot(const char* name) {
  const fs::path root = fs::temp_directory_path() / name;
  fs::remove_all(root);
  return root;
}

ClockRecord clock(const char* uid) {
  ClockRecord r;
  r.uid = uid;
  r.name = "kitchen";
  r.clockUrl = "http://192.168.1.39";
  return r;
}

const auto allFree = [](int) { return true; };

void aCodeIsSixDigitsAndPairsOnce() {
  uint32_t next = 42;
  PairingCode code([&next] { return next++; });
  const std::string first = code.current(0);
  check(first == "000042", "six digits with leading zeros");
  check(code.takeRenewed() && !code.takeRenewed(), "a new code is reported once");
  check(code.check(first, 10) == PairingCode::Check::Ok, "the code pairs");
  check(code.current(20) != first, "and is used up");
}

void aCodeExpires() {
  uint32_t next = 1;
  PairingCode code([&next] { return next++; });
  const std::string first = code.current(0);
  check(code.validForMs(1000) == PairingCode::kValidMs - 1000, "it counts down");
  check(code.current(PairingCode::kValidMs) != first, "after ten minutes there is a new one");
}

void fiveWrongGuessesLock() {
  uint32_t next = 7;
  PairingCode code([&next] { return next++; });
  const std::string first = code.current(0);
  for (int i = 0; i < PairingCode::kMaxFailures; ++i) code.check("999999", i);
  check(code.check(code.current(10), 10) == PairingCode::Check::Locked, "even the right code waits");
  check(code.current(10) != first, "and the guessed-at code is gone");
  check(code.check(code.current(PairingCode::kLockMs + 10), PairingCode::kLockMs + 10) == PairingCode::Check::Ok,
        "a minute later pairing works again");
}

void locksGrowUntilAClockPairs() {
  uint32_t next = 3;
  PairingCode code([&next] { return next++; });
  int64_t now = 0;
  const auto lockedFor = [&code, &now](int64_t ms) {
    for (int i = 0; i < PairingCode::kMaxFailures; ++i) code.check("bad", now);
    const bool held = code.check(code.current(now + ms - 1), now + ms - 1) == PairingCode::Check::Locked;
    now += ms;
    return held && code.check("bad", now) == PairingCode::Check::Wrong;
  };
  for (const int64_t minutes : {1, 2, 4, 8, 16, 32, 60, 60})
    check(lockedFor(minutes * 60 * 1000), "each lock lasts twice as long as the last, at most an hour");
  check(code.check(code.current(now), now) == PairingCode::Check::Ok, "the right code pairs once the lock ran out");
  check(lockedFor(PairingCode::kLockMs), "and the next lock lasts a minute again");
}

bool leftovers(const fs::path& dir) {
  for (const auto& entry : fs::directory_iterator(dir))
    if (entry.path().filename().string().find(".tmp.") != std::string::npos) return true;
  return false;
}

void keysArePrivateAndWrittenWhole() {
  const fs::path root = freshRoot("awtrix-hostd-key");
#if !defined(_WIN32)
  const mode_t previousMask = ::umask(0);
#endif
  ClockStore store(root);
  std::string error, text;
  check(store.load(error), "an empty state loads");
  ClockRecord out;
  check(store.pair(clock("abc123"), std::string(64, 'a'), allFree, out, error), "pairs under an open umask");
  check(awtrix::test::privateMode(store.keyFile("abc123"), 0600), "the key is private");
  check(awtrix::test::privateMode(store.clockDir("abc123") / "clock.json", 0600), "and so is the clock's address");
  check(awtrix::os::readFile(store.keyFile("abc123").string(), text) && text == std::string(64, 'a') + "\n",
        "the key file holds the whole key");
  check(!leftovers(store.clockDir("abc123")), "no temporary file stays behind");
#if !defined(_WIN32)
  if (::geteuid() != 0) {
    fs::permissions(store.clockDir("abc123"), fs::perms::owner_read | fs::perms::owner_exec);
    check(!store.pair(clock("abc123"), std::string(64, 'b'), allFree, out, error), "a key that cannot be written fails");
    fs::permissions(store.clockDir("abc123"), fs::perms::owner_all);
    check(store.keyOk("abc123", "Bearer " + std::string(64, 'a')), "and the old key stays in force");
    check(!leftovers(store.clockDir("abc123")), "without a temporary file");
  }
  ::umask(previousMask);
#endif
  fs::remove_all(root);
}

void pairingGivesPortsAndKeepsThem() {
  const fs::path root = freshRoot("awtrix-hostd-store");
  ClockStore store(root);
  std::string error;
  check(store.load(error), "an empty state loads");
  ClockRecord a, b, again;
  check(store.pair(clock("a1b2c3"), std::string(64, 'a'), allFree, a, error), "first clock pairs");
  check(store.pair(clock("d4e5f6"), std::string(64, 'b'), [](int port) { return port != 8302; }, b, error),
        "second clock pairs");
  check(a.port == 8301 && b.port == 8303, "ports start at 8301 and skip one in use");
  check(store.pair(clock("a1b2c3"), std::string(64, 'c'), allFree, again, error) && again.port == 8301,
        "pairing again keeps the port");
  check(awtrix::test::privateMode(store.keyFile("a1b2c3"), 0600), "the key is private");
  check(store.keyOk("a1b2c3", "Bearer " + std::string(64, 'c')), "the new key counts");
  check(!store.keyOk("a1b2c3", "Bearer " + std::string(64, 'a')), "the old one does not");

  ClockStore reloaded(root);
  check(reloaded.load(error) && reloaded.clocks().size() == 2, "both come back from disk");
  check(reloaded.find("d4e5f6") && reloaded.find("d4e5f6")->port == 8303, "with their ports");
  check(reloaded.find("a1b2c3") && reloaded.find("a1b2c3")->keyHex == std::string(64, 'c') &&
        again.keyHex == std::string(64, 'c'), "and their keys");
  check(awtrix::os::replaceFile(reloaded.keyFile("a1b2c3").string(), std::string(64, 'f') + "\n") &&
        reloaded.keyOk("a1b2c3", "Bearer " + std::string(64, 'c')) &&
        !reloaded.keyOk("a1b2c3", "Bearer " + std::string(64, 'f')), "a key is not read from disk again");

  check(reloaded.remove("a1b2c3", true), "a clock can be forgotten");
  check(fs::exists(reloaded.dataDir("a1b2c3")) && !fs::exists(reloaded.keyFile("a1b2c3")),
        "keeping its data drops the key");
  check(reloaded.remove("d4e5f6", false) && !fs::exists(reloaded.clockDir("d4e5f6")), "or drops everything");
  fs::remove_all(root);
}

void badUidsAreRefused() {
  const fs::path root = freshRoot("awtrix-hostd-uid");
  ClockStore store(root);
  std::string error;
  store.load(error);
  ClockRecord out;
  check(!store.pair(clock("../etc"), std::string(64, 'a'), allFree, out, error), "no path in a uid");
  check(!store.pair(clock(""), std::string(64, 'a'), allFree, out, error), "no empty uid");
  fs::remove_all(root);
}

}

int main() {
  aCodeIsSixDigitsAndPairsOnce();
  aCodeExpires();
  fiveWrongGuessesLock();
  locksGrowUntilAClockPairs();
  keysArePrivateAndWrittenWhole();
  pairingGivesPortsAndKeepsThem();
  badUidsAreRefused();
  return awtrix::test::finish("hostd-store");
}
