#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace awtrix::linux_script {

using Hash = std::array<uint8_t, 32>;

// sha256(sha256(header)) of an 80-byte block header, bytes as SHA-256 writes them.
void blockHash(const uint8_t header[80], uint8_t out[32]);
// True when hash, read as a little-endian 256-bit number, is at most target.
bool meets(const uint8_t hash[32], const uint8_t target[32]);
// The target of a pool difficulty, little-endian; difficulty 1 is 0xffff * 2^208.
Hash targetOf(double difficulty);
// The highest difficulty whose target the hash meets.
double difficultyOf(const uint8_t hash[32]);

struct PowEvent {
  bool done = false;
  uint32_t nonce = 0;
  Hash hash{};
};

// Scans a nonce range of one header on worker threads at SCHED_IDLE. Hits and the end of the
// range wait in a bounded queue for pop(); stop() and start() discard what is still queued.
class PowScanner {
 public:
  static constexpr std::size_t kQueue = 64;
  static constexpr uint32_t kBatch = 1u << 12;

  PowScanner() { best_.fill(0xff); }
  ~PowScanner() { stop(); }
  PowScanner(const PowScanner&) = delete;
  PowScanner& operator=(const PowScanner&) = delete;

  bool start(const uint8_t header[80], const uint8_t target[32], uint32_t first, uint32_t last, unsigned threads);
  void stop();
  bool pop(PowEvent& out);
  bool running() const { return active_.load() > 0; }
  uint64_t hashes() const { return hashes_.load(); }
  uint64_t dropped() const { return dropped_.load(); }
  Hash best() const;
  void resetBest();

 private:
  void work();
  void push(const PowEvent& event);

  uint8_t header_[80] = {};
  uint8_t target_[32] = {};
  uint64_t last_ = 0;
  std::atomic<uint64_t> next_{0};
  std::atomic<bool> stop_{false};
  std::atomic<unsigned> active_{0};
  std::atomic<uint64_t> hashes_{0};
  std::atomic<uint64_t> dropped_{0};
  mutable std::mutex mutex_;
  std::deque<PowEvent> events_;
  Hash best_{};
  std::vector<std::thread> workers_;
  std::condition_variable readyCv_;
  unsigned ready_ = 0;
  bool schedulingFailed_ = false;
};

}
