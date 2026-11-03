#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "core/script/AsyncQueue.h"
#include "core/script/ScriptServices.h"

namespace awtrix {

class ScriptHttpWorker : public script::IScriptHttp {
 public:
  using ResultFn = std::function<void(script::HttpResult)>;

  void begin(ResultFn onResult);

  bool request(const script::HttpRequest& req) override;
  void cancel(uint32_t id) override;

 private:
  static constexpr std::size_t kQueueCap = 16;

  struct Queued {
    script::HttpRequest req;
    uint8_t tightTries = 0;
    int64_t notBeforeMs = 0;
    int64_t queuedAtMs = 0;
  };

  bool startTask();
  static void taskEntry(void* self);
  void run();
  void giveUp(uint32_t id);
  void fetch(const script::HttpRequest& req, int64_t budgetMs);
  void fetchModbus(const script::HttpRequest& req);
  int64_t connectedForMs();

  script::AsyncQueue<Queued, kQueueCap> queue_;
  ResultFn onResult_;
  std::atomic<unsigned> pending_{0};
  bool taskStarted_ = false;
  std::atomic<uint32_t> running_{0};
  std::atomic<bool> stopRunning_{false};
  int64_t connectedAtMs_ = -1;
};

}
