#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "platform/linux/net/HttpClient.h"
#include "platform/linux/script/PerScriptStore.h"
#include "platform/linux/secrets/SecretSpec.h"
#include "platform/posix/PrivateStore.h"

namespace awtrix::secrets {

struct KeyStatus {
  std::string key;
  std::string label;
  std::string help;
  std::vector<std::string> hosts;
  bool set = false;
};

struct Status {
  bool declared = false;
  std::string invalid;
  std::vector<KeyStatus> keys;
};

class Service final : public script::PerScriptStore {
 public:
  static constexpr std::size_t kMaxValue = 512;

  Service(std::string directory, script::SourceReader source);

  Status status(const std::string& app);
  bool set(const std::string& app, const std::vector<std::pair<std::string, std::string>>& values,
           std::string& error);
  bool has(const std::string& app, const std::string& key);
  bool allows(const std::string& app, const std::string& key, const std::string& url);
  net::RequestFill fill(const std::string& app, std::string& url, net::RequestHeaders& headers, net::RequestBody& body);
  void dropStale() override;
  void sourceSaved(const std::string& app) override;
  void sourceRemoved(const std::string& app) override;
  bool eraseAll() override;

 private:
  struct Parsed {
    std::optional<Spec> spec;
    std::string invalid;
    bool unread = false;
  };

  const Parsed& parsedLocked(const std::string& app);
  void pruneLocked(const std::string& app);

  posix::PrivateStore store_;
  script::SourceReader source_;
  std::mutex mutex_;
  std::map<std::string, Parsed> specs_;
};

}
