#include "platform/posix/Files.h"
#include "platform/linux/script/CryptoMiner.h"

#include <cstdio>
#include <algorithm>
#include <cmath>
#include <thread>

#include "berry.h"
#include "core/script/BerryVM.h"

namespace awtrix::linux_script {
namespace {

CryptoMiner& selfOf(bvm* vm) { return *static_cast<CryptoMiner*>(script::BerryVM::nativeSelf(vm)); }

unsigned cores() { return std::max(1u, std::thread::hardware_concurrency()); }


}

CryptoMiner::CryptoMiner(std::function<int64_t()> clock) : clock_(std::move(clock)) {}

void CryptoMiner::install(script::ScriptExtensionHost& host) {
  host.defineNative("_crypto_mine", &CryptoMiner::start, this);
  host.defineNative("_crypto_mine_stop", &CryptoMiner::stop, this);
  host.defineNative("_crypto_mine_rate", &CryptoMiner::rate, this);
  host.defineNative("_crypto_mine_hashes", &CryptoMiner::hashes, this);
  host.defineNative("_crypto_mine_best", &CryptoMiner::best, this);
  host.defineNative("_crypto_mine_threads", &CryptoMiner::threads, this);
  host.defineNative("_crypto_target", &CryptoMiner::target, this);
  host.defineNative("_crypto_difficulty", &CryptoMiner::difficulty, this);
}

// True when the calling app owns the scanner; renews its lease.
bool CryptoMiner::mine(bvm* vm) {
  (void)vm;
  if (owner_ != script::ScriptExtensionHost::caller()) return false;
  touched_ = clock_();
  return true;
}

void CryptoMiner::release() {
  ++generation_;
  scanner_.stop();
  owner_.clear();
  samples_.clear();
}

void CryptoMiner::tick(script::ScriptExtensionHost& host, const RenderCtx* ctx) {
  if (owner_.empty()) return;
  const int64_t now = clock_();
  if (now - touched_ >= kLeaseMs) {
    host.call("_crypto_mine_drop", owner_, "");
    release();
    return;
  }
  if (samples_.empty() || now - samples_.back().first >= 100)
    samples_.emplace_back(now, scanner_.hashes());
  while (samples_.size() > 2 && now - samples_[1].first >= kRateWindowMs) samples_.pop_front();
  const uint64_t generation = generation_;
  const std::string owner = owner_;
  PowEvent e;
  for (int i = 0; i < kPerTick && scanner_.pop(e); ++i) {
    const std::string nonce = e.done ? "" : [&] {
      char buf[9];
      std::snprintf(buf, sizeof buf, "%08x", e.nonce);
      return std::string(buf);
    }();
    if (!host.deliver(owner, "crypto.mine", "_crypto_mine_dispatch", nonce, e.done ? "" : posix::hexBytes(e.hash.data(), 32), "", ctx)) {
      release();
      return;
    }
    if (generation_ != generation) return;
  }
}

void CryptoMiner::forget(script::ScriptExtensionHost& host, const std::string& app) {
  if (app != owner_) return;
  host.call("_crypto_mine_drop", app, "");
  release();
}

int CryptoMiner::start(bvm* vm) {
  CryptoMiner& self = selfOf(vm);
  const std::string& app = script::ScriptExtensionHost::caller();
  if (app.empty()) be_raise(vm, "value_error", "mine requires an app");
  if (!self.owner_.empty() && self.owner_ != app) {
    be_pushbool(vm, false);
    be_return(vm);
  }
  size_t headerSize = 0, targetSize = 0;
  if (!be_isbytes(vm, 1) || !be_isbytes(vm, 2)) be_raise(vm, "value_error", "mine sizes");
  const auto* header = static_cast<const uint8_t*>(be_tobytes(vm, 1, &headerSize));
  const auto* target = static_cast<const uint8_t*>(be_tobytes(vm, 2, &targetSize));
  if (!header || !target || headerSize != 80 || targetSize != 32) be_raise(vm, "value_error", "mine sizes");
  const int asked = be_isint(vm, 3) ? static_cast<int>(be_toint(vm, 3)) : 0;
  const unsigned threads = asked <= 0 ? cores() : std::min<unsigned>(static_cast<unsigned>(asked), cores());
  if (self.owner_ != app) {
    self.owner_ = app;
    self.base_ = self.scanner_.hashes();
    self.scanner_.resetBest();
  }
  self.touched_ = self.clock_();
  ++self.generation_;
  if (self.samples_.empty()) self.samples_.emplace_back(self.touched_, self.scanner_.hashes());
  if (!self.scanner_.start(header, target, 0, 0xffffffffu, threads)) {
    self.release();
    be_raise(vm, "runtime_error", "mine could not start idle workers");
  }
  be_pushbool(vm, true);
  be_return(vm);
}

int CryptoMiner::stop(bvm* vm) {
  CryptoMiner& self = selfOf(vm);
  const bool owner = self.mine(vm);
  if (owner) self.release();
  be_pushbool(vm, owner);
  be_return(vm);
}

int CryptoMiner::rate(bvm* vm) {
  CryptoMiner& self = selfOf(vm);
  double r = 0;
  if (self.mine(vm) && self.samples_.size() >= 2) {
    const auto& a = self.samples_.front();
    const auto& b = self.samples_.back();
    if (b.first > a.first) r = static_cast<double>(b.second - a.second) * 1000.0 / static_cast<double>(b.first - a.first);
  }
  be_pushreal(vm, static_cast<breal>(r));
  be_return(vm);
}

int CryptoMiner::hashes(bvm* vm) {
  CryptoMiner& self = selfOf(vm);
  const double n = self.mine(vm) ? static_cast<double>(self.scanner_.hashes() - self.base_) : 0.0;
  be_pushreal(vm, static_cast<breal>(n));
  be_return(vm);
}

int CryptoMiner::best(bvm* vm) {
  CryptoMiner& self = selfOf(vm);
  double d = 0;
  if (self.mine(vm)) {
    const Hash b = self.scanner_.best();
    bool any = false;
    for (uint8_t x : b) any = any || x != 0xff;
    d = any ? difficultyOf(b.data()) : 0.0;
  }
  be_pushreal(vm, static_cast<breal>(d));
  be_return(vm);
}

int CryptoMiner::threads(bvm* vm) {
  selfOf(vm).mine(vm);
  be_pushint(vm, static_cast<bint>(cores()));
  be_return(vm);
}

int CryptoMiner::target(bvm* vm) {
  selfOf(vm).mine(vm);
  if (!be_isnumber(vm, 1)) be_raise(vm, "value_error", "target takes a difficulty");
  const double d = static_cast<double>(be_toreal(vm, 1));
  if (!std::isfinite(d) || d <= 0) be_raise(vm, "value_error", "difficulty must be positive and finite");
  const Hash t = targetOf(d);
  be_pushbytes(vm, t.data(), t.size());
  be_return(vm);
}

int CryptoMiner::difficulty(bvm* vm) {
  selfOf(vm).mine(vm);
  size_t size = 0;
  const auto* h = be_isbytes(vm, 1) ? static_cast<const uint8_t*>(be_tobytes(vm, 1, &size)) : nullptr;
  if (!h || size != 32) be_raise(vm, "value_error", "difficulty takes 32 bytes");
  be_pushreal(vm, static_cast<breal>(difficultyOf(h)));
  be_return(vm);
}

}
