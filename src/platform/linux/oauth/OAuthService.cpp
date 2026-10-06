#include "platform/linux/oauth/OAuthService.h"

#include <algorithm>
#include <cctype>
#include <climits>

#include "platform/linux/oauth/OAuthText.h"

namespace awtrix::oauth {
namespace {

constexpr std::size_t kMaxClientId = 256;
constexpr std::size_t kMaxClientSecret = 512;
constexpr std::size_t kMaxCode = 2048;
// Without expires_in the token is taken to last an hour; it is renewed a minute early.
constexpr long long kDefaultLifetimeSeconds = 3600;
constexpr long long kRenewEarlyMs = 60000;

std::string bindingOf(const std::string& app, const Spec& spec) { return sha256Hex(app + "\n" + spec.line); }

bool blockedHeader(const std::string& name) {
  const std::string n = lowercase(name);
  return n == "authorization" || n == "host" || n == "content-length" || n == "transfer-encoding" ||
         n == "connection";
}

bool cleanHeader(const std::pair<std::string, std::string>& h) {
  return !h.first.empty() && h.first.size() <= 64 && h.second.size() <= 1024 &&
         std::all_of(h.first.begin(), h.first.end(), [](unsigned char c) { return std::isalnum(c) || c == '-'; }) &&
         std::all_of(h.second.begin(), h.second.end(), [](unsigned char c) { return c >= 32 && c != 127; });
}

bool signedIn(const Record& r) { return !r.refreshToken.empty() || !r.accessToken.empty(); }

bool knownMethod(const std::string& method) {
  static const char* const kMethods[] = {"GET", "POST", "PUT", "PATCH", "DELETE"};
  return std::any_of(std::begin(kMethods), std::end(kMethods), [&](const char* m) { return method == m; });
}

}

Service::Service(Vault& vault, Transport transport, std::function<std::string(const std::string&)> source,
                 std::function<long long()> clockMs)
    : vault_(vault), transport_(std::move(transport)), source_(std::move(source)), clock_(std::move(clockMs)) {}

Service::~Service() { stop(); }

void Service::begin() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (worker_.joinable()) return;
  stopping_ = false;
  worker_ = std::thread([this] { run(); });
}

void Service::stop() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
    jobs_.clear();
    ready_.notify_all();
  }
  if (interrupt_) interrupt_();
  if (worker_.joinable()) worker_.join();
}

Status Service::status(const std::string& app) {
  std::lock_guard<std::mutex> lock(mutex_);
  Status s;
  const auto spec = specLocked(app, &s.invalid);
  s.declared = spec.has_value() || !s.invalid.empty();
  if (!spec) return s;
  s.provider = hostOf(spec->authorize);
  s.scope = spec->scope;
  s.pkce = spec->pkce;
  Record rec;
  if (recordLocked(app, *spec, rec)) {
    s.clientId = rec.clientId;
    s.clientSecretSet = !rec.clientSecret.empty();
    if (signedIn(rec)) s.state = "signedIn";
    else if (!rec.lastError.empty()) s.state = "error";
    s.error = rec.lastError;
  }
  const auto p = pending_.find(app);
  if (p != pending_.end() && p->second.exchanging) s.state = "pending";
  return s;
}

bool Service::saveClient(const std::string& app, const std::optional<std::string>& clientId,
                         const std::optional<std::string>& clientSecret, bool clearSecret, std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto spec = specLocked(app);
  if (!spec) {
    error = "no @oauth line";
    return false;
  }
  if (clientId && (clientId->empty() || clientId->size() > kMaxClientId || !printable(*clientId))) {
    error = "invalid client id";
    return false;
  }
  if (clientSecret && (clientSecret->size() > kMaxClientSecret || !printable(*clientSecret))) {
    error = "invalid client secret";
    return false;
  }
  Record rec;
  if (!recordLocked(app, *spec, rec)) rec = Record{};
  rec.app = app;
  rec.binding = bindingOf(app, *spec);
  // Tokens belong to the client that got them.
  if (clientId && *clientId != rec.clientId) {
    rec.clientId = *clientId;
    rec.refreshToken.clear();
    rec.accessToken.clear();
    rec.lastError.clear();
    access_.erase(app);
  }
  if (clientSecret && !clientSecret->empty()) rec.clientSecret = *clientSecret;
  if (clearSecret) rec.clientSecret.clear();
  if (!storeLocked(app, rec)) {
    error = "storage failed";
    return false;
  }
  return true;
}

bool Service::start(const std::string& app, const std::string& returnOrigin, std::string& url, std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto spec = specLocked(app);
  if (!spec) {
    error = "no @oauth line";
    return false;
  }
  Record rec;
  if (!recordLocked(app, *spec, rec) || rec.clientId.empty()) {
    error = "client id missing";
    return false;
  }
  if (!spec->pkce && rec.clientSecret.empty()) {
    error = "client secret missing";
    return false;
  }
  const std::string nonce = randomToken(16), verifier = randomToken(48);
  if (nonce.empty() || verifier.empty()) {
    error = "no random numbers";
    return false;
  }
  Pending p;
  p.state = makeState(returnOrigin, app, nonce);
  p.verifier = verifier;
  p.expiresAt = clock_() + kPendingMs;
  url = authorizeUrl(*spec, rec.clientId, p.state, pkceChallenge(verifier));
  pending_[app] = std::move(p);
  return true;
}

bool Service::submitCode(const std::string& app, const std::string& code, const std::string& state,
                         std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto p = pending_.find(app);
  if (p == pending_.end() || p->second.exchanging || p->second.expiresAt <= clock_() || p->second.state != state) {
    error = "sign-in expired or unknown";
    return false;
  }
  if (code.empty() || code.size() > kMaxCode || !printable(code)) {
    error = "invalid code";
    return false;
  }
  p->second.exchanging = true;
  Job job;
  job.app = app;
  job.exchange = true;
  job.code = code;
  job.verifier = p->second.verifier;
  job.generation = generation_[app];
  jobs_.push_front(std::move(job));
  ready_.notify_one();
  return true;
}

void Service::signOut(const std::string& app) {
  std::lock_guard<std::mutex> lock(mutex_);
  access_.erase(app);
  pending_.erase(app);
  const auto spec = specLocked(app);
  Record rec;
  if (!spec || !recordLocked(app, *spec, rec)) return;
  rec.refreshToken.clear();
  rec.accessToken.clear();
  rec.lastError.clear();
  storeLocked(app, rec);
}

void Service::dropStale() {
  std::lock_guard<std::mutex> lock(mutex_);
  vault_.keepOnly([this](const Record& record) {
    const auto spec = parseSpec(source_(record.app));
    return spec && record.binding == bindingOf(record.app, *spec);
  });
  records_.clear();
}

bool Service::eraseAll() {
  std::lock_guard<std::mutex> lock(mutex_);
  records_.clear();
  access_.clear();
  pending_.clear();
  jobs_.clear();
  results_.clear();
  return vault_.eraseAll();
}

uint32_t Service::request(const std::string& app, const std::string& method, const std::string& url,
                          const std::string& body, const Headers& headers, std::size_t cap) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (stopping_ || !knownMethod(method)) return 0;
  const auto spec = specLocked(app);
  Record rec;
  if (!spec || !hostAllowed(*spec, url) || !recordLocked(app, *spec, rec) || !signedIn(rec)) return 0;
  const auto queued = std::count_if(jobs_.begin(), jobs_.end(), [&](const Job& j) { return j.app == app; });
  if (jobs_.size() >= kQueueTotal || static_cast<std::size_t>(queued) >= kQueuePerApp) return 0;
  Job job;
  job.call.method = method;
  job.call.url = url;
  job.call.body = body;
  job.call.cap = std::clamp<std::size_t>(cap, 1, kMaxCap);
  bool contentType = false;
  for (const auto& h : headers) {
    if (!cleanHeader(h) || blockedHeader(h.first)) return 0;
    contentType = contentType || lowercase(h.first) == "content-type";
    job.call.headers.push_back(h);
  }
  if (!body.empty() && !contentType) job.call.headers.emplace_back("Content-Type", "application/json");
  job.app = app;
  job.generation = generation_[app];
  job.id = nextId_++;
  if (job.id == 0) job.id = nextId_++;
  const uint32_t id = job.id;
  jobs_.push_back(std::move(job));
  ready_.notify_one();
  return id;
}

bool Service::ready(const std::string& app) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto spec = specLocked(app);
  Record rec;
  return spec && recordLocked(app, *spec, rec) && signedIn(rec);
}

bool Service::pop(Result& out) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (results_.empty()) return false;
  out = std::move(results_.front());
  results_.pop_front();
  return true;
}

void Service::forget(const std::string& app) {
  std::lock_guard<std::mutex> lock(mutex_);
  ++generation_[app];
  specs_.erase(app);
  records_.erase(app);
  jobs_.erase(std::remove_if(jobs_.begin(), jobs_.end(), [&](const Job& j) { return j.app == app && !j.exchange; }),
              jobs_.end());
  results_.erase(std::remove_if(results_.begin(), results_.end(), [&](const Result& r) { return r.app == app; }),
                 results_.end());
}

void Service::sourceSaved(const std::string& app) {
  std::lock_guard<std::mutex> lock(mutex_);
  specs_.erase(app);
  records_.erase(app);
  const auto spec = specLocked(app);
  Record unused;
  if (!spec) eraseLocked(app);
  else recordLocked(app, *spec, unused);
}

void Service::sourceRemoved(const std::string& app) {
  std::lock_guard<std::mutex> lock(mutex_);
  specs_.erase(app);
  eraseLocked(app);
}

std::optional<Spec> Service::specLocked(const std::string& app, std::string* invalid) {
  auto it = specs_.find(app);
  if (it == specs_.end()) {
    Parsed p;
    p.spec = parseSpec(source_(app), &p.invalid);
    it = specs_.emplace(app, std::move(p)).first;
  }
  if (invalid) *invalid = it->second.invalid;
  return it->second.spec;
}

// A record bound to another @oauth line or script name is erased.
bool Service::recordLocked(const std::string& app, const Spec& spec, Record& out) {
  auto it = records_.find(app);
  if (it == records_.end()) {
    Record r;
    it = records_.emplace(app, vault_.load(app, r) ? std::optional<Record>(r) : std::nullopt).first;
  }
  if (!it->second) return false;
  if (it->second->binding != bindingOf(app, spec)) {
    eraseLocked(app);
    return false;
  }
  out = *it->second;
  return true;
}

bool Service::storeLocked(const std::string& app, const Record& record) {
  if (!vault_.save(app, record)) return false;
  records_[app] = record;
  return true;
}

void Service::eraseLocked(const std::string& app) {
  vault_.erase(app);
  records_[app].reset();
  access_.erase(app);
  pending_.erase(app);
}

void Service::grantLocked(const std::string& app, const TokenResult& token) {
  const long long lifetime = token.expiresIn > 0 ? token.expiresIn : kDefaultLifetimeSeconds;
  access_[app] = {token.accessToken, clock_() + lifetime * 1000 - kRenewEarlyMs};
}

HttpReply Service::callUnlocked(std::unique_lock<std::mutex>& lock, const HttpCall& call) {
  lock.unlock();
  HttpReply reply = transport_(call);
  lock.lock();
  return reply;
}

// The record is read again after every network call: the HTTP side may have changed or erased it
// meanwhile.
Service::Renewal Service::refreshLocked(std::unique_lock<std::mutex>& lock, const std::string& app) {
  const auto spec = specLocked(app);
  Record rec;
  if (!spec || !recordLocked(app, *spec, rec)) return Renewal::Gone;
  if (rec.refreshToken.empty()) {
    if (rec.accessToken.empty()) return Renewal::Gone;
    access_[app] = {rec.accessToken, LLONG_MAX};
    return Renewal::Ok;
  }
  const HttpReply reply = callUnlocked(lock, refreshRequest(*spec, {rec.clientId, rec.clientSecret}, rec.refreshToken));
  const TokenResult token = parseTokenResponse(reply.status, reply.body);
  if (!recordLocked(app, *spec, rec)) return Renewal::Gone;
  if (token.ok) {
    if (!token.refreshToken.empty()) rec.refreshToken = token.refreshToken;
    rec.lastError.clear();
    grantLocked(app, token);
  } else {
    rec.lastError = token.error;
    if (token.permanent) rec.refreshToken.clear();
  }
  storeLocked(app, rec);
  return token.ok ? Renewal::Ok : token.permanent ? Renewal::Gone : Renewal::Offline;
}

// A token without a refresh token cannot be renewed: once the API refuses it, the sign-in is over.
void Service::revokeLocked(const std::string& app) {
  access_.erase(app);
  const auto spec = specLocked(app);
  Record rec;
  if (!spec || !recordLocked(app, *spec, rec) || !rec.refreshToken.empty()) return;
  rec.accessToken.clear();
  rec.lastError = "invalid_token";
  storeLocked(app, rec);
}

void Service::run() {
  std::unique_lock<std::mutex> lock(mutex_);
  for (;;) {
    ready_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
    if (stopping_) return;
    Job job = std::move(jobs_.front());
    jobs_.pop_front();
    if (job.exchange) runExchange(lock, job);
    else runRequest(lock, job);
  }
}

void Service::runExchange(std::unique_lock<std::mutex>& lock, const Job& job) {
  const auto spec = specLocked(job.app);
  Record rec;
  if (!spec || !recordLocked(job.app, *spec, rec)) {
    pending_.erase(job.app);
    return;
  }
  const HttpReply reply =
      callUnlocked(lock, exchangeRequest(*spec, {rec.clientId, rec.clientSecret}, job.code, job.verifier));
  const TokenResult token = parseTokenResponse(reply.status, reply.body);
  pending_.erase(job.app);
  if (!recordLocked(job.app, *spec, rec)) return;
  if (token.ok) {
    rec.refreshToken = token.refreshToken;
    rec.accessToken = token.refreshToken.empty() ? token.accessToken : std::string();
    rec.lastError.clear();
    grantLocked(job.app, token);
  } else {
    rec.lastError = token.error;
  }
  storeLocked(job.app, rec);
}

void Service::runRequest(std::unique_lock<std::mutex>& lock, const Job& job) {
  Result result;
  result.app = job.app;
  result.id = job.id;
  // A 401 can mean a token revoked early: renew once and try again.
  for (int attempt = 0; attempt < 2; ++attempt) {
    const auto access = access_.find(job.app);
    if (access == access_.end() || access->second.expiresAt <= clock_()) {
      const Renewal renewal = refreshLocked(lock, job.app);
      if (renewal != Renewal::Ok) {
        result.status = renewal == Renewal::Gone ? 401 : 0;
        result.body.clear();
        break;
      }
    }
    HttpCall call = job.call;
    call.headers.emplace_back("Authorization", "Bearer " + access_[job.app].token);
    const HttpReply reply = callUnlocked(lock, call);
    if (stopping_) return;
    result.status = reply.status;
    result.body = reply.body.size() > job.call.cap ? reply.body.substr(0, job.call.cap) : reply.body;
    if (reply.status != 401) break;
    result.body.clear();
    revokeLocked(job.app);
  }
  if (generation_[job.app] != job.generation) return;
  results_.push_back(std::move(result));
}

}
