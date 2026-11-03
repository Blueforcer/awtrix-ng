#include "../support.h"
// The threads behind a listener's connections: they start with the work, end after it and never hold
// a connection back that a thread could serve.
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "platform/linux/host/OnDemandWorkers.h"
#include "platform/linux/host/vendor/httplib.h"

using awtrix::host_http::OnDemandWorkers;
using awtrix::test::check;
using namespace std::chrono_literals;

namespace {

// Jobs that wait until they are released.
class Gate {
 public:
  std::function<void()> job() {
    return [this] {
      std::unique_lock<std::mutex> lock(mutex_);
      ++entered_;
      changed_.notify_all();
      changed_.wait(lock, [this] { return open_; });
      ++finished_;
      changed_.notify_all();
    };
  }
  bool waitEntered(int count, std::chrono::milliseconds limit = 5000ms) {
    std::unique_lock<std::mutex> lock(mutex_);
    return changed_.wait_for(lock, limit, [&] { return entered_ >= count; });
  }
  bool waitFinished(int count) {
    std::unique_lock<std::mutex> lock(mutex_);
    return changed_.wait_for(lock, 5000ms, [&] { return finished_ >= count; });
  }
  int entered() {
    std::lock_guard<std::mutex> lock(mutex_);
    return entered_;
  }
  void open() {
    std::lock_guard<std::mutex> lock(mutex_);
    open_ = true;
    changed_.notify_all();
  }

 private:
  std::mutex mutex_;
  std::condition_variable changed_;
  int entered_ = 0, finished_ = 0;
  bool open_ = false;
};

bool eventually(const std::function<bool()>& condition, std::chrono::milliseconds limit = 5000ms) {
  const auto end = std::chrono::steady_clock::now() + limit;
  while (!condition()) {
    if (std::chrono::steady_clock::now() > end) return false;
    std::this_thread::sleep_for(2ms);
  }
  return true;
}

void growsWithTheWorkAndShrinksAfterIt() {
  OnDemandWorkers workers(1, 3, 40ms);
  check(workers.threads() == 1, "one thread is there from the start");
  Gate first;
  check(workers.enqueue(first.job()) && first.waitEntered(1), "a connection is served by it");
  check(workers.threads() == 1, "without another thread");
  check(workers.enqueue(first.job()) && workers.enqueue(first.job()) && first.waitEntered(3),
        "connections at the same time get a thread each");
  check(workers.threads() == 3, "up to the limit");
  check(workers.enqueue(first.job()), "a connection beyond the limit is accepted");
  std::this_thread::sleep_for(60ms);
  check(first.entered() == 3 && workers.threads() == 3, "and waits, no fourth thread starts");
  first.open();
  check(first.waitFinished(4), "it is served when a thread is free");
  check(eventually([&] { return workers.threads() == 1; }), "idle threads end, down to the one that stays");
  Gate again;
  check(workers.enqueue(again.job()) && workers.enqueue(again.job()) && again.waitEntered(2), "and they come back");
  again.open();
  check(again.waitFinished(2), "for the next connections");
}

void aThreadThatWaitsIsUsedFirst() {
  OnDemandWorkers workers(2, 8, 10s);
  std::atomic<int> done{0};
  for (int i = 0; i < 20; ++i) {
    check(workers.enqueue([&done] { ++done; }), "a short job");
    check(eventually([&] { return done == i + 1; }), "is done");
  }
  check(workers.threads() <= 3, "jobs one after the other start no thread per job");
}

void nothingStartsWithoutAThread() {
  bool allow = false;
  std::vector<std::thread> started;
  OnDemandWorkers workers(1, 3, 40ms, [&](std::function<void()> body) {
    if (!allow) return false;
    started.emplace_back(std::move(body));
    return true;
  });
  check(workers.threads() == 0, "a thread that cannot start does not count");
  std::atomic<int> done{0};
  check(!workers.enqueue([&done] { ++done; }), "with no thread at all a connection is refused, its socket closes");
  allow = true;
  Gate gate;
  check(workers.enqueue(gate.job()) && gate.waitEntered(1), "the next one starts a thread");
  allow = false;
  check(workers.enqueue([&done] { ++done; }), "with one busy and none to start a connection is kept");
  std::this_thread::sleep_for(20ms);
  check(done == 0, "it waits for the busy thread");
  gate.open();
  check(eventually([&] { return done == 1; }), "and is served then");
  workers.shutdown();
  for (std::thread& thread : started) thread.join();
}

void shutdownServesWhatWaitsAndEnds() {
  std::atomic<int> done{0};
  {
    OnDemandWorkers workers(1, 2, 10s);
    Gate gate;
    check(workers.enqueue(gate.job()) && workers.enqueue(gate.job()) && gate.waitEntered(2), "both threads busy");
    for (int i = 0; i < 5; ++i) check(workers.enqueue([&done] { ++done; }), "connections in line");
    std::thread stopper([&] { workers.shutdown(); });
    std::this_thread::sleep_for(20ms);
    gate.open();
    stopper.join();
    check(done == 5, "shutdown serves the line");
    check(workers.threads() == 0, "and every thread has left");
    check(!workers.enqueue([&done] { ++done; }), "after it nothing is taken");
  }
  { OnDemandWorkers idle(3, 3, 10s); }
}

// Thread starts and ends racing the connections that keep arriving: every one is served.
void everyConnectionIsServedThroughTheChurn() {
  std::atomic<int> done{0};
  constexpr int kJobs = 2000;
  {
    OnDemandWorkers workers(0, 4, 1ms);
    for (int i = 0; i < kJobs; ++i) {
      while (!workers.enqueue([&done] { ++done; })) std::this_thread::yield();
      if (i % 7 == 0) std::this_thread::sleep_for(1ms);
    }
    check(eventually([&] { return done == kJobs; }), "all served: " + std::to_string(done.load()));
  }
  check(done == kJobs, "no job twice");
}

// Parallel requests through a real listener, keep-alive included.
void servesARealListener() {
  httplib::Server server;
  server.new_task_queue = [] { return new OnDemandWorkers(1, 8, 200ms); };
  std::atomic<int> inFlight{0}, most{0};
  server.Get("/", [&](const httplib::Request&, httplib::Response& response) {
    const int now = ++inFlight;
    int seen = most.load();
    while (now > seen && !most.compare_exchange_weak(seen, now)) {
    }
    std::this_thread::sleep_for(20ms);
    --inFlight;
    response.set_content("ok", "text/plain");
  });
  const int port = server.bind_to_any_port("127.0.0.1");
  std::thread listener([&] { server.listen_after_bind(); });
  server.wait_until_ready();
  std::atomic<int> answers{0};
  std::vector<std::thread> clients;
  for (int c = 0; c < 12; ++c)
    clients.emplace_back([&, port] {
      httplib::Client client("127.0.0.1", port);
      client.set_keep_alive(true);
      for (int r = 0; r < 3; ++r) {
        const auto answer = client.Get("/");
        if (answer && answer->status == 200) ++answers;
      }
    });
  for (std::thread& client : clients) client.join();
  check(answers == 36, "every request is answered: " + std::to_string(answers.load()));
  check(most.load() <= 8 && most.load() >= 2, "in parallel, at most the limit: " + std::to_string(most.load()));
  server.stop();
  listener.join();
}

}

int main() {
  growsWithTheWorkAndShrinksAfterIt();
  aThreadThatWaitsIsUsedFirst();
  nothingStartsWithoutAThread();
  shutdownServesWhatWaitsAndEnds();
  everyConnectionIsServedThroughTheChurn();
  servesARealListener();
  if (awtrix::test::failures()) {
    std::printf("%d failure(s)\n", awtrix::test::failures());
    return 1;
  }
  std::puts("on-demand workers: all passed");
  return 0;
}
