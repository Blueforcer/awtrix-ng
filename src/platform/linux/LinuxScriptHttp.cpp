#include "platform/linux/LinuxScriptHttp.h"
#include <chrono>
#include "platform/linux/host/vendor/httplib.h"
#include "core/script/HttpBodyFilter.h"
#include "core/script/ModbusTcp.h"
#include "platform/linux/host/compat/WiFiClient.h"
#include "system/Log.h"

namespace awtrix {
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
    std::string origin, path;
    net::splitUrl(req.url, origin, path);
    httplib::Client client(origin);
    const std::string method = req.method.empty() ? "GET" : req.method;
    // Redirects are followed for reads only, as on the ESP32: a POST is never sent twice.
    const bool reading = method == "GET" || method == "HEAD";
    net::configure(client, {script::kHttpTimeoutMs, 3, reading});
    interrupt_.watch(client);
    script::HttpBodyFilter filter;
    filter.begin(req.find, req.keep, script::httpBodyCap(req.maxBytes));
    const auto response = net::send(client, method, path, req.body, req.headers,
                                   [&](const char* bytes, std::size_t n) {
      filter.feed(bytes, n);
      return !filter.outOfRoom();
    });
    interrupt_.release();
    if (response) {
      result.status = response->status;
      result.ok = filter.matched() && !filter.outOfRoom();
      if (result.ok) result.body = std::move(filter.body());
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (stopping_) return;
    }
    if (result_) result_(std::move(result));
  }
}
}
