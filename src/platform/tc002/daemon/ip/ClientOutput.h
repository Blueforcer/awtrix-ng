#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace awtrix {
namespace tc002d {
namespace ip {

// BusyBox writes udhcpc's progress and its errors alike to stderr. True for the progress lines
// of an ordinary acquisition or renewal, which the daemon already reports from the callback.
bool routineClientMessage(std::string_view line);

// Splits the client's stderr into lines, drops routine ones and passes the rest on, at most
// budget lines per window; the number of lines held back is reported when the window ends.
class ClientOutput {
 public:
  using Emit = std::function<void(const std::string& line)>;
  static constexpr std::size_t kMaxLine = 512;

  explicit ClientOutput(Emit emit, int64_t windowMs = 60 * 1000, unsigned budget = 20)
      : emit_(std::move(emit)), windowMs_(windowMs), budget_(budget) {}

  void feed(const char* data, std::size_t size, int64_t nowMs);
  void finish(int64_t nowMs);

 private:
  void line(std::string text, int64_t nowMs);

  Emit emit_;
  int64_t windowMs_;
  unsigned budget_;
  std::string pending_;
  int64_t windowStartMs_ = -1;
  unsigned emitted_ = 0, suppressed_ = 0;
};

}
}
}
