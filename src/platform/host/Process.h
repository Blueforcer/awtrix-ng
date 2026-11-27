#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace awtrix::os {

class ChildProcess {
 public:
  ChildProcess() = default;
  ~ChildProcess() { stop(); }
  ChildProcess(ChildProcess&& other) noexcept;
  ChildProcess& operator=(ChildProcess&& other) noexcept;
  ChildProcess(const ChildProcess&) = delete;
  ChildProcess& operator=(const ChildProcess&) = delete;

  bool start(const std::vector<std::string>& args, const std::string& log);
  bool running() const { return id_ > 0; }
  long id() const { return id_; }
  bool ended(int& status);
  void stop(int graceMs = 5000);

 private:
  long id_ = -1;
#if defined(_WIN32)
  void* handle_ = nullptr;
#endif
};

void stopOnRequest(std::atomic<bool>& stop);
void lowerThreadPriority(int nice);
std::string hostName();

}
