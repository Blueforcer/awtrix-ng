#pragma once

// Just enough Arduino and FreeRTOS to compile an ESP32-only worker on the host and drive its real
// code. A task is a detached std::thread; vTaskDelete() ends it by throwing to the thread's root.

#if defined(_WIN32)
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <utility>

using BaseType_t = int;
using UBaseType_t = unsigned;
using TickType_t = uint32_t;
using TaskHandle_t = void*;

constexpr BaseType_t pdPASS = 1;
constexpr BaseType_t errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY = -1;

constexpr TickType_t pdMS_TO_TICKS(uint32_t ms) { return ms; }

namespace fake {

struct TaskDeleted {};

inline std::atomic<int> tasksCreated{0};
inline std::atomic<int> failCreates{0};
inline std::atomic<int> tasksAlive{0};
inline std::atomic<int> tasksReturned{0};
inline std::atomic<bool> runTasks{true};
inline std::atomic<bool> stopTasks{false};

inline void resetTasks() {
  tasksCreated = 0;
  failCreates = 0;
  tasksReturned = 0;
  runTasks = true;
  stopTasks = false;
}

inline bool waitForNoTasks() {
  const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (tasksAlive.load() > 0) {
    if (std::chrono::steady_clock::now() > until) return false;
    std::this_thread::yield();
  }
  return true;
}

}

inline BaseType_t xTaskCreatePinnedToCore(void (*entry)(void*), const char*, uint32_t, void* argument,
                                          UBaseType_t, TaskHandle_t*, BaseType_t) {
  if (fake::failCreates.load() > 0) {
    --fake::failCreates;
    return errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY;
  }
  ++fake::tasksCreated;
  if (!fake::runTasks) return pdPASS;
  ++fake::tasksAlive;
  std::thread([entry, argument] {
    try {
      entry(argument);
      // On FreeRTOS a task function must not return.
      ++fake::tasksReturned;
    } catch (const fake::TaskDeleted&) {
    }
    --fake::tasksAlive;
  }).detach();
  return pdPASS;
}

inline void vTaskDelete(TaskHandle_t) { throw fake::TaskDeleted{}; }

inline void vTaskDelay(TickType_t) {
  if (fake::stopTasks) throw fake::TaskDeleted{};
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

inline unsigned long millis() {
  return static_cast<unsigned long>(std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
}

class String {
 public:
  explicit String(std::string text) : text_(std::move(text)) {}
  const char* c_str() const { return text_.c_str(); }

 private:
  std::string text_;
};

class IPAddress {
 public:
  IPAddress() = default;
  IPAddress(uint32_t address) : address_(address) {}
  IPAddress& operator=(uint32_t address) { address_ = address; return *this; }
  operator uint32_t() const { return address_; }
  String toString() const {
    char text[16];
    std::snprintf(text, sizeof text, "%u.%u.%u.%u", address_ & 255u, (address_ >> 8) & 255u,
                  (address_ >> 16) & 255u, address_ >> 24);
    return String(text);
  }

 private:
  uint32_t address_ = 0;
};

class Stream {
 public:
  virtual ~Stream() = default;
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t*, size_t) = 0;
  virtual int available() = 0;
  virtual int read() = 0;
  virtual int peek() = 0;
  virtual void flush() = 0;
};
