#include "system/ScriptHttpWorker.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>

#include <esp_heap_caps.h>

#include <algorithm>
#include <utility>

#include "core/script/HttpBodyFilter.h"
#include "core/script/ScriptServices.h"
#include "system/HeapCaps.h"
#include "system/HeapProbe.h"
#include "system/Log.h"
#include "system/MonotonicClock.h"

namespace awtrix {

namespace {

constexpr uint32_t kStackBytes = 10240;
constexpr UBaseType_t kPriority = 1;
constexpr BaseType_t kCore = 0;

constexpr uint16_t kConnectTimeoutMs = 5000;
constexpr uint16_t kSocketTimeoutMs = 5000;
constexpr TickType_t kIdlePollTicks = pdMS_TO_TICKS(25);

// Breathing room between back-to-back fetches: a TLS session leaves the internal heap fragmented,
// and starting the next one immediately is how the "heap too tight" path gets hit.
constexpr TickType_t kFetchGapTicks = pdMS_TO_TICKS(1500);
constexpr TickType_t kRequeueTicks = pdMS_TO_TICKS(500);

// Lets HTTPClient stream the response through the filter, so only the bytes a script actually asked
// for are ever held in memory. Read side is stubbed out; this is write-only.
class FilterSink : public Stream {
 public:
  explicit FilterSink(script::HttpBodyFilter& filter) : filter_(filter) {}

  size_t write(uint8_t b) override {
    const char c = static_cast<char>(b);
    filter_.feed(&c, 1);
    return 1;
  }
  size_t write(const uint8_t* data, size_t len) override {
    filter_.feed(reinterpret_cast<const char*>(data), len);
    return len;
  }

  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}

 private:
  script::HttpBodyFilter& filter_;
};

}

void ScriptHttpWorker::begin(ResultFn onResult) {
  if (started_) return;
  onResult_ = std::move(onResult);
  if (!onResult_) return;
  started_ = true;
  xTaskCreatePinnedToCore(taskEntry, "scripthttp", kStackBytes, this, kPriority, nullptr, kCore);
}

// Called from the script VM on the main loop. Queues and returns immediately; the answer arrives
// later through the result callback. False means it was not even accepted.
bool ScriptHttpWorker::request(const script::HttpRequest& req) {
  if (!started_) return false;
  if (!WiFi.isConnected()) return false;
  if (req.url.rfind("http://", 0) != 0 && req.url.rfind("https://", 0) != 0) return false;
  if (req.method.empty()) return false;

  if (pending_.load(std::memory_order_relaxed) >= kQueueCap) return false;

  script::HttpRequest queued = req;
  if (queued.maxBytes == 0 || queued.maxBytes > script::kMaxHttpBody)
    queued.maxBytes = script::kMaxHttpBody;

  pending_.fetch_add(1, std::memory_order_relaxed);
  queue_.push(Queued{std::move(queued)});
  return true;
}

void ScriptHttpWorker::taskEntry(void* self) {
  static_cast<ScriptHttpWorker*>(self)->run();
}

void ScriptHttpWorker::run() {
  for (;;) {
    Queued q;
    if (!queue_.pop(q)) {
      vTaskDelay(kIdlePollTicks);
      continue;
    }
    // Right after a join the Wi-Fi stack is still holding buffers it will hand back shortly, so a
    // TLS handshake attempted now sees far less heap than it will in a moment. Wait it out.
    const bool https = q.req.url.rfind("https://", 0) == 0;
    if (https && script::tlsBootGraceActive(connectedForMs())) {
      queue_.push(std::move(q));
      vTaskDelay(kRequeueTicks);
      continue;
    }
    if (monotonicMs() < q.notBeforeMs) {
      queue_.push(std::move(q));
      vTaskDelay(kRequeueTicks);
      continue;
    }
    const std::size_t freeHeap = heap_caps_get_free_size(kGuardHeapCaps);
    const std::size_t largestBlock = heap_caps_get_largest_free_block(kGuardHeapCaps);
    if (!script::fetchFits(https, freeHeap, largestBlock)) {
      // Give the heap a few chances to recover, then drop the request and report an empty result
      // rather than leaving the script waiting on a callback that never comes.
      if (script::shouldRetryTightFetch(q.tightTries)) {
        ++q.tightTries;
        q.notBeforeMs = monotonicMs() + script::kTightRetryDelayMs;
        queue_.push(std::move(q));
        vTaskDelay(kRequeueTicks);
        continue;
      }
      logf("script http: heap too tight for TLS (%u free, %u largest) after %u tries, skipped %s",
           static_cast<unsigned>(freeHeap), static_cast<unsigned>(largestBlock),
           static_cast<unsigned>(q.tightTries) + 1, q.req.url.c_str());
      script::HttpResult res;
      res.id = q.req.id;
      onResult_(std::move(res));
      const bool moreQueued = pending_.fetch_sub(1, std::memory_order_relaxed) > 1;
      if (moreQueued) vTaskDelay(kFetchGapTicks);
      continue;
    }
    fetch(q.req);
    const bool moreQueued = pending_.fetch_sub(1, std::memory_order_relaxed) > 1;
    if (moreQueued) vTaskDelay(kFetchGapTicks);
  }
}

int64_t ScriptHttpWorker::connectedForMs() {
  if (!WiFi.isConnected()) {
    connectedAtMs_ = -1;
    return 0;
  }
  const int64_t now = monotonicMs();
  if (connectedAtMs_ < 0) connectedAtMs_ = now;
  return now - connectedAtMs_;
}

// Runs on the worker task and blocks for as long as the request takes. The clients are stack-local
// on purpose, so a TLS session's memory is handed straight back when this returns.
void ScriptHttpWorker::fetch(const script::HttpRequest& req) {
  script::HttpResult res;
  res.id = req.id;

  const bool https = req.url.rfind("https://", 0) == 0;

#ifdef AWTRIX_HEAP_PROBE
  const std::size_t watchFree = heap_caps_get_free_size(kGuardHeapCaps);
  const std::size_t watchLargest = heap_caps_get_largest_free_block(kGuardHeapCaps);
  probe::watchBegin();
#endif

  WiFiClient plain;
  WiFiClientSecure secure;
  if (https) {
    secure.setInsecure();
    secure.setTimeout(kSocketTimeoutMs / 1000);
  } else {
    plain.setTimeout(kSocketTimeoutMs / 1000);
  }
  WiFiClient& client = https ? static_cast<WiFiClient&>(secure) : plain;

  HTTPClient http;
  http.setConnectTimeout(kConnectTimeoutMs);
  http.setTimeout(kSocketTimeoutMs);
  // Connection reuse would keep a socket, and for https a whole TLS context, alive between
  // unrelated script fetches; the heap is worth more here than the handshake.
  http.setReuse(false);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (http.begin(client, req.url.c_str())) {
    for (const auto& h : req.headers) http.addHeader(h.first.c_str(), h.second.c_str());

    const int code = http.sendRequest(
        req.method.c_str(),
        reinterpret_cast<uint8_t*>(const_cast<char*>(req.body.data())), req.body.size());
    if (code > 0) {
      res.status = code;
      script::HttpBodyFilter filter;
      filter.begin(req.find, req.keep, req.maxBytes);
      FilterSink sink(filter);
      http.writeToStream(&sink);
      res.ok = filter.matched();
      if (res.ok) res.body = std::move(filter.body());
    } else {
      logdbg("script http: %s %s -> transport error %d", req.method.c_str(), req.url.c_str(),
             code);
    }
    http.end();
  } else {
    logdbg("script http: cannot open %s", req.url.c_str());
  }

#ifdef AWTRIX_HEAP_PROBE
  {
    const probe::Watch w = probe::watchEnd();
    std::string host = req.url;
    const std::string::size_type at = host.find("://");
    if (at != std::string::npos) host.erase(0, at + 3);
    host.resize(std::min(host.size(), host.find('/')));
    logf("probe fetch %s: b %u lg %u low %u max %u n %u/%u", host.c_str(),
         static_cast<unsigned>(watchFree), static_cast<unsigned>(watchLargest),
         static_cast<unsigned>(w.lowWater), static_cast<unsigned>(w.maxAlloc),
         static_cast<unsigned>(w.count), static_cast<unsigned>(w.bytes));
  }
#endif

  onResult_(std::move(res));
}

}
