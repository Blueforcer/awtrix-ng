#include "platform/linux/LinuxScriptHttp.h"
#include <chrono>
#include "platform/linux/host/vendor/httplib.h"
#include "core/script/ModbusTcp.h"
#include "core/script/ScriptBindings.h"
#include "platform/linux/host/compat/WiFiClient.h"
#include "platform/posix/StartThread.h"
#include "system/Log.h"

namespace awtrix {
LinuxScriptHttp::~LinuxScriptHttp() { stop(); }
void LinuxScriptHttp::begin(std::function<void(script::HttpResult)> result) {
  std::lock_guard<std::mutex> lock(mutex_);
  result_ = std::move(result);
}
void LinuxScriptHttp::stop() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
    queue_.clear();
    // Also shuts a socket the worker opens only after this, so a slow header or body cannot
    // postpone process shutdown.
    requests_.interrupt();
    ready_.notify_all();
  }
  if (worker_.joinable()) worker_.join();
}
bool LinuxScriptHttp::request(const script::HttpRequest& req) {
  if (script::modbus::isUrl(req.url)) {
    script::modbus::Read read;
    if (req.method != "GET" || !script::modbus::parse(req.url, read)) return false;
  } else {
    if (!net::validUrl(req.url)) return false;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (stopping_ || !result_) return false;
  if (queue_.size() >= 8) {
    if (script::modbus::isUrl(req.url)) logf("modbus: queue full");
    return false;
  }
  if (!worker_.joinable() && !posix::startThread(worker_, [this] { run(); })) return false;
  queue_.push_back({script::BindingScope::currentScript(), req});
  ready_.notify_one();
  return true;
}
void LinuxScriptHttp::run() {
  for (;;) {
    Job job;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      ready_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
      if (stopping_) return;
      job = std::move(queue_.front()); queue_.pop_front();
      requests_.rearm();
    }
    script::HttpRequest& req = job.request;
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
    // Redirects are followed for reads only, as on the ESP32: a POST is never sent twice.
    const bool reading = method == "GET" || method == "HEAD";
    net::FilteredAnswer answer;
    net::RequestBody body(std::move(req.body));
    requests_.run(job.owner, std::move(req.url), req.headers, body, {script::kHttpTimeoutMs, 3, reading},
                  [&](httplib::Client& client, const std::string& target) {
      answer = net::sendFiltered(client, method, target, std::move(body), req.headers,
                                 {req.find, req.keep, req.maxBytes});
    });
    result.status = answer.status;
    result.ok = answer.ok;
    result.body = std::move(answer.body);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (stopping_) return;
    }
    if (result_) result_(std::move(result));
  }
}
}
