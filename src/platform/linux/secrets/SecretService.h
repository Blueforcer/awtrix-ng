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
#include "platform/linux/secrets/SecretSpec.h"
#include "platform/posix/PrivateStore.h"

namespace awtrix::secrets {

struct KeyStatus {
  std::string key;
  std::string label;
  std::string help;
  bool set = false;
};

struct Status {
  bool declared = false;
  std::string invalid;
  std::vector<KeyStatus> keys;
};

class Service {
 public:
  static constexpr std::size_t kMaxValue = 512;
  static constexpr std::size_t kMaxRecord = 32768;

  Service(posix::PrivateStore& store, std::function<std::string(const std::string& app)> source);

  Status status(const std::string& app);
  bool set(const std::string& app, const std::vector<std::pair<std::string, std::string>>& values,
           std::string& error);
  bool has(const std::string& app, const std::string& key);
  net::RequestFill fill(const std::string& app, std::string& url, net::RequestHeaders& headers);
  void dropStale();
  void sourceSaved(const std::string& app);
  void sourceRemoved(const std::string& app);
  bool eraseAll();

 private:
  struct Parsed {
    std::optional<Spec> spec;
    std::string invalid;
  };

  const Parsed& parsedLocked(const std::string& app);
  void pruneLocked(const std::string& app);

  posix::PrivateStore& store_;
  std::function<std::string(const std::string&)> source_;
  std::mutex mutex_;
  std::map<std::string, Parsed> specs_;
};

}
