#pragma once

#include <functional>
#include <string>

#include "platform/posix/PrivateStore.h"

namespace awtrix::oauth {

struct Record {
  std::string app;
  std::string binding;
  std::string clientId;
  std::string clientSecret;
  std::string refreshToken;
  // Kept only when the provider hands out no refresh token.
  std::string accessToken;
  std::string lastError;
};

// One private file per app in a 0700 directory outside the data root, so no file route,
// listing or backup reaches it.
class Vault {
 public:
  explicit Vault(std::string directory);

  bool load(const std::string& app, Record& out) const;
  bool save(const std::string& app, const Record& record);
  bool erase(const std::string& app) { return store_.erase(app); }
  bool eraseAll() { return store_.eraseAll(); }
  // Erases every record keep() turns down, and every file that is no readable record.
  void keepOnly(const std::function<bool(const Record& record)>& keep);

 private:
  posix::PrivateStore store_;
};

}
