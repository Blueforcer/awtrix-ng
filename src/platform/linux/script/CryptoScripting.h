#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "core/script/ScriptExtension.h"
#include "platform/linux/script/CryptoMiner.h"

namespace awtrix::linux_script {

// `import crypto` for scripts: digests, HMAC, PBKDF2 and random bytes over bytes or strings, and
// the search for a block nonce.
class CryptoScripting : public script::ScriptExtension {
 public:
  explicit CryptoScripting(std::function<int64_t()> clock) : miner_(std::move(clock)) {}

  std::vector<std::string> modules() const override { return {"crypto"}; }
  void install(script::ScriptExtensionHost& host) override;
  void tick(script::ScriptExtensionHost& host, const RenderCtx* ctx) override { miner_.tick(host, ctx); }
  void forget(script::ScriptExtensionHost& host, const std::string& app) override { miner_.forget(host, app); }

  CryptoMiner& miner() { return miner_; }

 private:
  CryptoMiner miner_;
};

}
