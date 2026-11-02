#include "../support.h"
#include "ThreadStacks.h"

#include <atomic>
#include <filesystem>
#include <future>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#include "platform/posix/WorkerThread.h"

using awtrix::posix::WorkerThread;
using awtrix::test::check;
using awtrix::test::require;

namespace {

std::ptrdiff_t threads() {
  return std::distance(std::filesystem::directory_iterator("/proc/self/task"), std::filesystem::directory_iterator{});
}

void startsWithTheFirstTask() {
  const std::ptrdiff_t before = threads();
  WorkerThread worker;
  check(threads() == before, "a worker without tasks runs no thread");
  std::promise<std::thread::id> ran;
  std::future<std::thread::id> id = ran.get_future();
  worker.post([&ran] { ran.set_value(std::this_thread::get_id()); });
  const std::thread::id runner = id.get();
  check(runner != std::this_thread::get_id() && threads() == before + 1, "the first task starts the worker's thread");
}

void aThreadThatCannotStartRefusesTheTask() {
  WorkerThread worker;
  std::atomic<int> ran{0};
  {
    const awtrix::test::ThreadStacks impossible(awtrix::test::kImpossibleStackBytes);
    check(!worker.post([&ran] { ran += 1; }), "a thread that cannot start refuses the task");
  }
  std::promise<void> done;
  check(worker.post([&done] { done.set_value(); }), "the next task starts it after all");
  done.get_future().wait();
  check(ran == 0, "the refused task never runs");
}

void runsTasksInOrder() {
  std::vector<int> order;
  std::promise<void> done;
  std::future<void> ended = done.get_future();
  {
    WorkerThread worker;
    for (int i = 0; i < 100; ++i) worker.post([&order, i] { order.push_back(i); });
    worker.post([&worker, &order, &done] {
      worker.post([&order, &done] {
        order.push_back(100);
        done.set_value();
      });
    });
    ended.wait();
  }
  bool inOrder = order.size() == 101;
  for (std::size_t i = 0; inOrder && i < order.size(); ++i) inOrder = order[i] == static_cast<int>(i);
  check(inOrder, "tasks run one after another in the order they were posted, also those a task posts: " +
                     std::to_string(order.size()));
}

void destructionWaitsForTheRunningTask() {
  std::promise<void> entered, release;
  std::future<void> running = entered.get_future();
  std::shared_future<void> released = release.get_future().share();
  std::atomic<bool> finished{false};
  std::thread opener;
  {
    WorkerThread worker;
    worker.post([&entered, released, &finished] {
      entered.set_value();
      released.wait();
      finished = true;
    });
    running.wait();
    opener = std::thread([&release] { release.set_value(); });
  }
  opener.join();
  check(finished, "a worker that goes away lets its running task finish first");
}

}

int main() {
  aThreadThatCannotStartRefusesTheTask();
  startsWithTheFirstTask();
  runsTasksInOrder();
  destructionWaitsForTheRunningTask();
  return awtrix::test::finish("posix-worker-thread");
}
