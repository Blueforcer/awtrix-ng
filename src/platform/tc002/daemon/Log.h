#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

// Line log of awtrix-tc002d: "<monotonic seconds>.<ms> <component>: <message>". The file is
// bounded: past the cap it is renamed to "<path>.1" (replacing the previous rotation) and
// restarted. Lines are mirrored to stderr and handed to the sink set with forwardTo, if any; a
// line the sink logs itself is not handed over again.
namespace awtrix {
namespace tc002d {

class Log {
 public:
  static constexpr std::size_t kDefaultCapBytes = 256 * 1024;
  static constexpr std::size_t kMaxLine = 1024;

  static bool open(const std::string& path, std::size_t capBytes = kDefaultCapBytes);
  static void mirrorToStderr(bool enabled);
  static void close();
  using Sink = std::function<void(const char* component, std::string_view message)>;
  static void forwardTo(Sink sink);

  static void line(const char* component, const char* format, ...) __attribute__((format(printf, 2, 3)));
  static void text(const char* component, std::string_view message);
};

}
}
