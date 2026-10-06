#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "core/script/ScriptExtension.h"
#include "platform/linux/script/PowScanner.h"

namespace awtrix::linux_script {

// The mining calls of `import crypto`: one app at a time mines on the scanner. Its hits reach it
// on the script tick; five seconds without a mining call from it end the job.
class CryptoMiner {
 public:
  static constexpr int64_t kLeaseMs = 5000;
  static constexpr int64_t kRateWindowMs = 5000;
  static constexpr int kPerTick = 16;

  explicit CryptoMiner(std::function<int64_t()> clock);

  void install(script::ScriptExtensionHost& host);
  void tick(script::ScriptExtensionHost& host, const RenderCtx* ctx);
  void forget(script::ScriptExtensionHost& host, const std::string& app);

  PowScanner& scanner() { return scanner_; }

 private:
  static int start(bvm* vm);
  static int stop(bvm* vm);
  static int rate(bvm* vm);
  static int hashes(bvm* vm);
  static int best(bvm* vm);
  static int threads(bvm* vm);
  static int target(bvm* vm);
  static int difficulty(bvm* vm);
  bool mine(bvm* vm);
  void release();

  std::function<int64_t()> clock_;
  PowScanner scanner_;
  std::string owner_;
  int64_t touched_ = 0;
  uint64_t base_ = 0;
  uint64_t generation_ = 0;
  std::deque<std::pair<int64_t, uint64_t>> samples_;
};

}
