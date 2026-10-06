#pragma once

#include <functional>
#include <string>
#include <utility>

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
  explicit Vault(std::string directory) : dir_(std::move(directory)) {}

  bool load(const std::string& app, Record& out) const;
  bool save(const std::string& app, const Record& record);
  bool erase(const std::string& app);
  bool eraseAll();
  // Erases every record keep() turns down, and every file that is no readable record.
  void keepOnly(const std::function<bool(const Record& record)>& keep);

 private:
  std::string path(const std::string& app) const;
  std::string dir_;
};

}
