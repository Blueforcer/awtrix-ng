#include "../support.h"
// The proof-of-work scanner: the block hash against the genesis block, targets, the mining kernel
// against the block hash, and scans whose hits and best hash are recomputed one by one.
#include <cmath>
#include "platform/posix/sha256.h"
#include <algorithm>
#include <chrono>
#include <thread>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "platform/linux/script/PowScanner.h"
#include "platform/linux/script/Sha256d.h"

using namespace awtrix::linux_script;

namespace {

int& failures = awtrix::test::failures();

using awtrix::test::check;

std::vector<uint8_t> unhex(const char* s) {
  std::vector<uint8_t> out;
  for (size_t i = 0; s[i] && s[i + 1]; i += 2) out.push_back(static_cast<uint8_t>(std::stoi(std::string(s + i, 2), nullptr, 16)));
  return out;
}

std::string hexReversed(const uint8_t* b, size_t n) {
  static const char* d = "0123456789abcdef";
  std::string out;
  for (size_t i = n; i-- > 0;) {
    out += d[b[i] >> 4];
    out += d[b[i] & 15];
  }
  return out;
}

const char* kGenesis =
    "01000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "3ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a"
    "29ab5f49"
    "ffff001d"
    "1dac2b7c";

void genesisHashes() {
  const auto header = unhex(kGenesis);
  uint8_t hash[32];
  blockHash(header.data(), hash);
  check(hexReversed(hash, 32) == "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f",
        "the genesis header hashes to the genesis block id");
}

void targets() {
  const Hash one = targetOf(1.0);
  bool shape = one[26] == 0xff && one[27] == 0xff;
  for (int i = 0; i < 32; ++i)
    if (i != 26 && i != 27 && one[i] != 0) shape = false;
  check(shape, "difficulty 1 is 0xffff * 2^208");
  check(std::fabs(difficultyOf(one.data()) - 1.0) < 1e-9, "and reads back as difficulty 1");
  const Hash half = targetOf(2.0);
  check(half[27] == 0x7f && half[26] == 0xff && half[25] == 0x80, "difficulty 2 halves it");
  const Hash easy = targetOf(1e-30);
  bool all = true;
  for (uint8_t b : easy) all = all && b == 0xff;
  check(all, "a target beyond 2^256 saturates");
  check(meets(one.data(), one.data()), "a hash equal to the target meets it");
  Hash above = one;
  above[28] = 1;
  check(!meets(above.data(), one.data()), "a larger hash does not");
}

void setNonce(uint8_t header[80], uint32_t nonce) {
  for (int i = 0; i < 4; ++i) header[76 + i] = static_cast<uint8_t>(nonce >> (8 * i));
}

uint32_t topOf(const uint8_t hash[32]) {
  return static_cast<uint32_t>(hash[31]) << 24 | static_cast<uint32_t>(hash[30]) << 16 |
         static_cast<uint32_t>(hash[29]) << 8 | hash[28];
}

// sha256(sha256(header)) through the generic SHA-256 API, independent of the kernel.
void plainDouble(const uint8_t header[80], uint8_t out[32]) {
  uint8_t first[32];
  sha256_state state;
  sha256_init(&state);
  sha256_update(&state, header, 80);
  sha256_final(&state, first);
  sha256_init(&state);
  sha256_update(&state, first, sizeof first);
  sha256_final(&state, out);
}

void kernelMatchesBlockHash() {
  auto genesis = unhex(kGenesis);
  uint8_t hash[32];
  plainDouble(genesis.data(), hash);
  check(Sha256d(genesis.data()).top(0x7c2bac1d) == topOf(hash) && topOf(hash) == 0,
        "the kernel's top word of the genesis block is zero");
  uint32_t seed = 0x9e3779b9;
  auto next = [&] { return seed = seed * 1664525u + 1013904223u; };
  uint8_t header[80], full[32];
  bool same = true, whole = true;
  for (int sample = 0; sample < 4096; ++sample) {
    for (auto& b : header) b = static_cast<uint8_t>(next() >> 24);
    const Sha256d kernel(header);
    for (uint32_t nonce : {next(), next(), next(), 0u, 1u, 0x80000000u, 0xffffffffu}) {
      setNonce(header, nonce);
      plainDouble(header, hash);
      kernel.hash(nonce, full);
      same = same && kernel.top(nonce) == topOf(hash);
      whole = whole && std::memcmp(full, hash, 32) == 0;
    }
  }
  check(same, "the kernel's top word equals sha256d's for random headers and nonces");
  check(whole, "the kernel's whole hash equals sha256d's for random headers and nonces");
}

struct Scan {
  std::vector<std::pair<uint32_t, Hash>> hits;
  Hash best;
};

// Every hit and the lowest hash of a range, one blockHash after another.
Scan plainScan(std::vector<uint8_t> header, const Hash& target, uint32_t first, uint32_t last) {
  Scan out;
  out.best.fill(0xff);
  for (uint64_t n = first; n <= last; ++n) {
    setNonce(header.data(), static_cast<uint32_t>(n));
    Hash h;
    blockHash(header.data(), h.data());
    if (meets(h.data(), target.data())) out.hits.emplace_back(static_cast<uint32_t>(n), h);
    if (!meets(out.best.data(), h.data())) out.best = h;
  }
  return out;
}

}

std::vector<PowEvent> drain(PowScanner& s) {
  std::vector<PowEvent> out;
  for (int spins = 0; spins < 20000; ++spins) {
    PowEvent e;
    while (s.pop(e)) {
      out.push_back(e);
      if (e.done) return out;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return out;
}

void findsTheGenesisNonce() {
  auto header = unhex(kGenesis);
  const uint32_t nonce = 0x7c2bac1d;
  const Hash target = [] {
    Hash t{};
    t[26] = 0xff;
    t[27] = 0xff;
    return t;
  }();
  for (unsigned threads : {1u, 3u}) {
    PowScanner s;
    s.start(header.data(), target.data(), nonce - 5000, nonce + 5000, threads);
    const auto events = drain(s);
    check(events.size() == 2 && !events[0].done && events[0].nonce == nonce && events[1].done,
          "the scan finds the genesis nonce once and then ends");
    check(s.hashes() == 10001, "every nonce of the range is hashed once");
    check(!s.running(), "and the workers are gone");
  }
}

void everyHitIsReal() {
  auto header = unhex(kGenesis);
  const Hash target = targetOf(1.0 / 65536);
  const uint32_t first = 1000, last = first + (1u << 18) - 1;
  size_t expected = 0;
  for (uint64_t n = first; n <= last; ++n) {
    std::memcpy(header.data() + 76, &n, 4);
    uint8_t h[32];
    blockHash(header.data(), h);
    if (meets(h, target.data())) ++expected;
  }
  PowScanner s;
  s.start(header.data(), target.data(), first, last, 4);
  const auto events = drain(s);
  size_t hits = 0;
  bool real = true;
  for (const auto& e : events) {
    if (e.done) continue;
    ++hits;
    std::memcpy(header.data() + 76, &e.nonce, 4);
    uint8_t h[32];
    blockHash(header.data(), h);
    real = real && std::memcmp(h, e.hash.data(), 32) == 0 && meets(h, target.data());
  }
  check(expected > 0 && hits == expected, "the scan reports exactly the hits a plain loop finds");
  check(real, "and each one hashes to what it reports");
  check(difficultyOf(s.best().data()) >= 1.0 / 65536, "the best hash is at least as good as a hit");
}

// The scan against a plain loop: the same hits with the same hashes, the same best hash.
void scanMatchesPlainLoop(const char* what, const Hash& target, uint32_t first, uint32_t last, unsigned threads) {
  const auto header = unhex(kGenesis);
  const Scan plain = plainScan(header, target, first, last);
  PowScanner s;
  s.start(header.data(), target.data(), first, last, threads);
  const auto events = drain(s);
  std::vector<std::pair<uint32_t, Hash>> hits;
  for (const auto& e : events)
    if (!e.done) hits.emplace_back(e.nonce, e.hash);
  std::sort(hits.begin(), hits.end());
  const bool ok = !events.empty() && events.back().done && plain.hits.size() < PowScanner::kQueue &&
                  s.dropped() == 0 && hits == plain.hits && s.best() == plain.best &&
                  s.hashes() == uint64_t{last} - first + 1;
  if (!ok) std::printf("  %s: %zu hits, plain loop %zu\n", what, hits.size(), plain.hits.size());
  check(ok, what);
}

void filterKeepsEveryHitAndTheBest() {
  Hash all;
  all.fill(0xff);
  scanMatchesPlainLoop("every hash hits", all, 0, 62, 1);
  Hash most = all;
  most[31] = 0xf0;
  scanMatchesPlainLoop("nearly every hash hits", most, 1000, 1062, 2);
  scanMatchesPlainLoop("a hit in 256", targetOf(1.0 / (65536.0 * 256)), 5000, 5000 + 8191, 3);
  scanMatchesPlainLoop("difficulty 1", targetOf(1.0), 0x7c2bac1d - 3000, 0x7c2bac1d + 3000, 2);
  const Hash none{};
  scanMatchesPlainLoop("an impossible target, one worker", none, 0, 65535, 1);
  scanMatchesPlainLoop("an impossible target, three workers", none, 0, 65535, 3);

  const Hash best = plainScan(unhex(kGenesis), none, 0, 4095).best;
  Hash below = best;
  for (int i = 0; i < 32; ++i)
    if (below[i]-- != 0) break;
  check(topOf(below.data()) == topOf(best.data()), "the target below the best hash shares its top word");
  scanMatchesPlainLoop("a target just below the best hash", below, 0, 4095, 2);
  scanMatchesPlainLoop("a target equal to the best hash", best, 0, 4095, 2);

  auto header = unhex(kGenesis);
  std::vector<Hash> hashes(4096);
  for (uint32_t n = 0; n < hashes.size(); ++n) {
    setNonce(header.data(), n);
    blockHash(header.data(), hashes[n].data());
  }
  auto lowest = [&](size_t from) {
    return std::min_element(hashes.begin() + from, hashes.end(), [](const Hash& a, const Hash& b) {
      return !meets(b.data(), a.data());
    });
  };
  const size_t first = lowest(0) - hashes.begin();
  check(first + 1 < hashes.size(), "the lowest hash is not the last");
  const Hash later = *lowest(first + 1);
  scanMatchesPlainLoop("a hit whose top word is the bound after a better one", later, 0, 4095, 1);
}

void matchesIndependentSha() {
  uint8_t header[80], first[32], plain[32], optimized[32];
  uint32_t seed = 0x12345678;
  for (int sample = 0; sample < 256; ++sample) {
    for (auto& b : header) { seed = seed * 1664525u + 1013904223u; b = static_cast<uint8_t>(seed >> 24); }
    sha256_state state;
    sha256_init(&state); sha256_update(&state, header, sizeof header); sha256_final(&state, first);
    sha256_init(&state); sha256_update(&state, first, sizeof first); sha256_final(&state, plain);
    blockHash(header, optimized);
    check(std::memcmp(plain, optimized, 32) == 0, "midstate hash matches the generic SHA API");
  }
}

void stopEndsQuietly() {
  auto header = unhex(kGenesis);
  const Hash target{};
  PowScanner s;
  s.start(header.data(), target.data(), 0, 0xffffffffu, 2);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  check(s.running(), "a long scan runs");
  s.stop();
  PowEvent e;
  check(!s.running() && !s.pop(e), "stop ends it without a done event");
  s.start(header.data(), target.data(), 0, 99, 1);
  const auto events = drain(s);
  check(events.size() == 1 && events[0].done, "and the scanner can start again");
}


void nonceBoundaryAndOverflow() {
  auto h = unhex(kGenesis);
  Hash all; all.fill(0xff);
  PowScanner scanner;
  scanner.start(h.data(), all.data(), 0xffffffffu - 7, 0xffffffffu, 3);
  const auto events = drain(scanner);
  check(events.size() == 9 && events.back().done && scanner.hashes() == 8, "UINT32_MAX is included exactly once");
  scanner.start(h.data(), all.data(), 0, 999, 2);
  for (int i = 0; i < 1000 && scanner.running(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  const auto full = drain(scanner);
  check(full.size() == PowScanner::kQueue + 1 && full.back().done, "a full queue retains completion");
  check(scanner.dropped() == 1000 - PowScanner::kQueue, "overflow counts dropped hits");
  scanner.start(h.data(), all.data(), 1, 0, 1);
  const auto empty = drain(scanner);
  check(empty.size() == 1 && empty.front().done, "empty range completes");
}

int main(int argc, char** argv) {
  if (argc > 1 && std::string(argv[1]) == "bench") {
    auto h = unhex(kGenesis);
    Hash zero{};
    PowScanner scanner;
    const auto start = std::chrono::steady_clock::now();
    scanner.start(h.data(), zero.data(), 0, (1u << 24) - 1, 1);
    const auto result = drain(scanner);
    if (result.empty() || !result.back().done) return 1;
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("%.0f H/s (%llu hashes)\n", scanner.hashes() / seconds, static_cast<unsigned long long>(scanner.hashes()));
    return 0;
  }
  nonceBoundaryAndOverflow();
  matchesIndependentSha();
  genesisHashes();
  targets();
  kernelMatchesBlockHash();
  findsTheGenesisNonce();
  everyHitIsReal();
  filterKeepsEveryHitAndTheBest();
  stopEndsQuietly();
  return failures == 0 ? 0 : 1;
}
