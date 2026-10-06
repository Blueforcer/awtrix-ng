#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "platform/linux/oauth/OAuthFlow.h"
#include "platform/linux/oauth/OAuthSpec.h"
#include "platform/linux/oauth/OAuthVault.h"

namespace awtrix::oauth {

struct HttpReply {
  int status = 0;
  std::string body;
};
using Transport = std::function<HttpReply(const HttpCall&)>;

struct Status {
  bool declared = false;
  std::string invalid;
  std::string provider;
  std::string scope;
  std::string clientId;
  bool clientSecretSet = false;
  bool pkce = false;
  // signedOut, pending, signedIn or error
  std::string state = "signedOut";
  std::string error;
};

struct Result {
  std::string app;
  uint32_t id = 0;
  int status = 0;
  std::string body;
};

// Sign-ins of every script. Secrets live here and in the vault, never in the script VM. One worker
// makes every network call, so a rotating refresh token is never used twice.
class Service {
 public:
  static constexpr std::size_t kQueuePerApp = 8;
  static constexpr std::size_t kQueueTotal = 16;
  static constexpr std::size_t kMaxCap = 262144;
  static constexpr long long kPendingMs = 10 * 60 * 1000;

  Service(Vault& vault, Transport transport, std::function<std::string(const std::string& app)> source,
          std::function<long long()> clockMs);
  ~Service();
  void begin();
  void stop();
  // Ends a network call in flight when stopping.
  void setInterrupt(std::function<void()> interrupt) { interrupt_ = std::move(interrupt); }

  Status status(const std::string& app);
  bool saveClient(const std::string& app, const std::optional<std::string>& clientId,
                  const std::optional<std::string>& clientSecret, bool clearSecret, std::string& error);
  bool start(const std::string& app, const std::string& returnOrigin, std::string& url, std::string& error);
  bool submitCode(const std::string& app, const std::string& code, const std::string& state, std::string& error);
  void signOut(const std::string& app);
  // At start: erases the sign-ins of scripts that are gone or whose @oauth line changed.
  void dropStale();
  bool eraseAll();

  // 0 when refused: not signed in, a host the @oauth line does not list, a bad header or a full queue.
  uint32_t request(const std::string& app, const std::string& method, const std::string& url,
                   const std::string& body, const Headers& headers, std::size_t cap);
  bool ready(const std::string& app);
  bool pop(Result& out);
  // The app was reinstalled or stopped: what it asked for is dropped.
  void forget(const std::string& app);
  // A script's source reaches the disk only after it installed; these tell the service when it
  // changed there. A removed script's sign-in is erased.
  void sourceSaved(const std::string& app);
  void sourceRemoved(const std::string& app);

 private:
  enum class Renewal { Ok, Gone, Offline };
  struct Pending {
    std::string state;
    std::string verifier;
    long long expiresAt = 0;
    bool exchanging = false;
  };
  struct Access {
    std::string token;
    long long expiresAt = 0;
  };
  struct Parsed {
    std::optional<Spec> spec;
    std::string invalid;
  };
  struct Job {
    std::string app;
    uint32_t id = 0;
    uint32_t generation = 0;
    HttpCall call;
    std::string code;
    std::string verifier;
    bool exchange = false;
  };

  std::optional<Spec> specLocked(const std::string& app, std::string* invalid = nullptr);
  bool recordLocked(const std::string& app, const Spec& spec, Record& out);
  bool storeLocked(const std::string& app, const Record& record);
  void eraseLocked(const std::string& app);
  void grantLocked(const std::string& app, const TokenResult& token);
  HttpReply callUnlocked(std::unique_lock<std::mutex>& lock, const HttpCall& call);
  Renewal refreshLocked(std::unique_lock<std::mutex>& lock, const std::string& app);
  void revokeLocked(const std::string& app);
  void run();
  void runExchange(std::unique_lock<std::mutex>& lock, const Job& job);
  void runRequest(std::unique_lock<std::mutex>& lock, const Job& job);

  Vault& vault_;
  Transport transport_;
  std::function<std::string(const std::string&)> source_;
  std::function<long long()> clock_;
  std::function<void()> interrupt_;
  std::mutex mutex_;
  std::condition_variable ready_;
  std::thread worker_;
  bool stopping_ = false;
  uint32_t nextId_ = 1;
  std::map<std::string, Parsed> specs_;
  // The vault's records as last read or written; only this service touches the vault.
  std::map<std::string, std::optional<Record>> records_;
  std::map<std::string, Pending> pending_;
  std::map<std::string, Access> access_;
  std::map<std::string, uint32_t> generation_;
  std::deque<Job> jobs_;
  std::deque<Result> results_;
};

}
