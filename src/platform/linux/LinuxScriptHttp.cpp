#include "platform/linux/LinuxScriptHttp.h"
#include <algorithm>
#include "platform/linux/host/vendor/httplib.h"
#include "core/script/ModbusTcp.h"
#include "core/script/ScriptBindings.h"
#include "platform/linux/host/compat/WiFiClient.h"
#include "platform/posix/StartThread.h"
#include "system/Log.h"

namespace awtrix {
namespace {
constexpr std::size_t kMaxQueued = 8;
constexpr std::size_t kMaxQueuedBodyBytes = 4 * 1024 * 1024;
constexpr uint32_t kMaxTimeoutSec = 120;
}
LinuxScriptHttp::~LinuxScriptHttp() { stop(); }
void LinuxScriptHttp::begin(std::function<void(script::HttpResult)> result) {
  std::lock_guard<std::mutex> lock(mutex_);
  result_ = std::move(result);
}
void LinuxScriptHttp::stop() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
    for (Lane* lane : {&quick_, &patient_}) {
      lane->queue.clear();
      // Also shuts a socket the worker opens only after this, so a slow header or body cannot
      // postpone process shutdown.
      lane->requests.interrupt();
      lane->ready.notify_all();
    }
  }
  for (Lane* lane : {&quick_, &patient_})
    if (lane->worker.joinable()) lane->worker.join();
}
bool LinuxScriptHttp::request(const script::HttpRequest& req) {
  if (req.timeoutSec > kMaxTimeoutSec) return false;
  const bool modbus = script::modbus::isUrl(req.url);
  if (modbus) {
    script::modbus::Read read;
    if (req.method != "GET" || !script::modbus::parse(req.url, read)) return false;
  } else {
    if (!net::validUrl(req.url)) return false;
  }
  Lane& lane = req.timeoutSec && !modbus ? patient_ : quick_;
  std::lock_guard<std::mutex> lock(mutex_);
  if (stopping_ || !result_) return false;
  if (lane.queue.size() >= kMaxQueued || req.body.size() > kMaxQueuedBodyBytes - queuedBodyBytes_) {
    if (modbus) logf("modbus: queue full");
    return false;
  }
  if (!lane.worker.joinable() && !posix::startThread(lane.worker, [this, &lane] { run(lane); })) return false;
  queuedBodyBytes_ += req.body.size();
  lane.queue.push_back({script::BindingScope::currentScript(), req, std::chrono::steady_clock::now()});
  lane.ready.notify_one();
  return true;
}
void LinuxScriptHttp::cancel(uint32_t id) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (Lane* lane : {&quick_, &patient_}) {
    const auto job = std::find_if(lane->queue.begin(), lane->queue.end(),
                                  [id](const Job& queued) { return queued.request.id == id; });
    if (job != lane->queue.end()) {
      queuedBodyBytes_ -= job->request.body.size();
      lane->queue.erase(job);
    }
    if (lane->running == id) lane->requests.interrupt();
  }
}
void LinuxScriptHttp::run(Lane& lane) {
  for (;;) {
    Job job;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      lane.ready.wait(lock, [&] { return stopping_ || !lane.queue.empty(); });
      if (stopping_) return;
      job = std::move(lane.queue.front()); lane.queue.pop_front();
      queuedBodyBytes_ -= job.request.body.size();
      lane.running = job.request.id;
      lane.requests.rearm();
    }
    script::HttpRequest& req = job.request;
    script::HttpResult result;
    result.id = req.id;
    const int64_t budgetMs = script::httpBudgetMs(
        req.timeoutSec, std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - job.queuedAt).count());
    if (budgetMs <= 0) {
      if (result_) result_(std::move(result));
      continue;
    }
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
    net::ClientLimits limits{budgetMs, 3, reading};
    if (req.timeoutSec) limits.ioTimeoutSec = static_cast<int>(req.timeoutSec);
    lane.requests.run(job.owner, std::move(req.url), req.headers, body, limits,
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
