#include "platform/linux/LinuxScriptHttp.h"
#include <chrono>
#include <algorithm>
#include "platform/linux/tls/TrustStore.h"
#include "core/net/Url.h"
#include "platform/linux/host/vendor/httplib.h"
#include "core/script/HttpBodyFilter.h"
#include "core/script/ModbusTcp.h"
#include "platform/linux/host/compat/WiFiClient.h"
#include "system/Log.h"

namespace awtrix {
namespace {
// Redirects create a new client and a new verifier. A destination's custom trust never follows
// a Location header, even to the same host, and credentials never cross origins.
std::string redirectUrl(const std::string& current, std::string location) {
  const auto parsed = net::parseUrl(current);
  if (!parsed || location.empty()) return {};
  const auto hash = location.find('#');
  if (hash != location.npos) location.resize(hash);
  if (location.compare(0, 2, "//") == 0) return std::string(parsed->scheme) + ':' + location;
  if (location.find("://") != location.npos) return location;
  if (location.empty()) return current;
  if (location.front() == '/') return parsed->origin() + location;
  std::string path = parsed->requestTarget();
  if (location.front() == '?') path.resize(path.find('?') == path.npos ? path.size() : path.find('?'));
  else path.resize(path.substr(0, path.find('?')).rfind('/') + 1);
  return parsed->origin() + path + location;
}
}

LinuxScriptHttp::~LinuxScriptHttp() { stop(); }
void LinuxScriptHttp::begin(std::function<void(script::HttpResult)> result) {
  result_ = std::move(result);
  worker_ = std::thread([this] { run(); });
}
void LinuxScriptHttp::stop() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
    queue_.clear();
    // Also shuts a socket the worker opens only after this, so a slow header or body cannot
    // postpone process shutdown.
    interrupt_.interrupt();
    ready_.notify_all();
  }
  if (worker_.joinable()) worker_.join();
}
bool LinuxScriptHttp::request(const script::HttpRequest& req) {
  if (script::modbus::isUrl(req.url)) {
    script::modbus::Read read;
    if (req.method != "GET" || !script::modbus::parse(req.url, read)) return false;
  } else {
    std::string origin, target;
    if (!net::splitUrl(req.url, origin, target)) return false;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (stopping_ || !worker_.joinable()) return false;
  if (queue_.size() >= 8) {
    if (script::modbus::isUrl(req.url)) logf("modbus: queue full");
    return false;
  }
  queue_.push_back(req);
  ready_.notify_one();
  return true;
}
void LinuxScriptHttp::run() {
  for (;;) {
    script::HttpRequest req;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      ready_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
      if (stopping_) return;
      req = std::move(queue_.front()); queue_.pop_front();
      interrupt_.rearm();
    }
    script::HttpResult result;
    result.id = req.id;
    if (script::modbus::isUrl(req.url)) {
      const auto started = std::chrono::steady_clock::now();
      const char* diagnostic = "invalid request";
      script::modbus::Read read;
      WiFiClient client;
      if (script::modbus::parse(req.url, read)) {
        diagnostic = "connection failed";
        if (client.connect(read.host.c_str(), read.port)) {
          result = script::modbus::exchange(client, read, req.id, [] {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
          }, [this, &client] {
            {
              std::lock_guard<std::mutex> lock(mutex_);
              if (stopping_) { client.stop(); return; }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
          }, &diagnostic);
        }
      }
      client.stop();
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return;
      }
      if (!result.ok) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count();
        const std::string report = script::modbus::failureReport(
            read, diagnostic, result.status, static_cast<unsigned long>(elapsed));
        logf("%s", report.c_str());
      }
      if (result_) result_(std::move(result));
      continue;
    }
    const std::string method = req.method.empty() ? "GET" : req.method;
    const bool reading = method == "GET" || method == "HEAD";
    std::string url = req.url;
    auto headers = req.headers;
    const auto started = std::chrono::steady_clock::now();
    for (unsigned hop = 0; hop <= 20; ++hop) {
      std::string origin, path;
      if (!net::splitUrl(url, origin, path)) break;
      const auto parsed = net::parseUrl(url);
      std::string host(parsed->host);
      if (host.front() == '[') host = host.substr(1, host.size() - 2);
      const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - started).count();
      if (elapsed >= script::kHttpTimeoutMs) break;
      httplib::Client client(origin);
      net::configure(client, {script::kHttpTimeoutMs - elapsed, 3, false});
      bool customTrust = false;
      if (trust_ && parsed->tls()) tls::useHostTlsTrust(client, trust_, host, &customTrust);
      interrupt_.watch(client);
      script::HttpBodyFilter filter;
      filter.begin(req.find, req.keep, script::httpBodyCap(req.maxBytes));
      const auto response = net::send(client, method, path, req.body, headers,
                                     [&](const char* bytes, std::size_t n) {
        filter.feed(bytes, n);
        return !filter.outOfRoom();
      });
      interrupt_.release();
      if (!response) break;
      const int status = response->status;
      const bool redirect = status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
      if (reading && redirect && !customTrust && (!trust_ || !parsed->tls() || !trust_->custom(host))) {
        if (hop == 20) break;
        const auto next = redirectUrl(url, response->get_header_value("Location"));
        const auto address = net::parseUrl(next);
        if (!address || address->userinfo) break;
        // Even a public server cannot redirect a request into host-specific private trust.
        std::string nextHost(address->host);
        if (nextHost.front() == '[') nextHost = nextHost.substr(1, nextHost.size() - 2);
        if (trust_ && address->tls() && trust_->custom(nextHost)) break;
        if (origin != address->origin()) {
          headers.erase(std::remove_if(headers.begin(), headers.end(), [](const auto& header) {
            return httplib::detail::case_ignore::equal(header.first, std::string("Authorization")) ||
                httplib::detail::case_ignore::equal(header.first, std::string("Cookie"));
          }), headers.end());
        }
        url = next;
        continue;
      }
      result.status = status;
      result.ok = filter.matched() && !filter.outOfRoom();
      if (result.ok) result.body = std::move(filter.body());
      break;
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (stopping_) return;
    }
    if (result_) result_(std::move(result));
  }
}
}
