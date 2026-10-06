#include "platform/tc002/daemon/RuntimeLog.h"

#include <algorithm>
#include <cerrno>
#include <sys/socket.h>
#include <unistd.h>

#include "platform/tc002/daemon/Log.h"

namespace awtrix::tc002d {
namespace {
constexpr std::size_t kMaxOutputLine = 512;
constexpr char kOutputComponent[] = "awtrix-linux";
}

bool RuntimeLog::forwarded(std::string_view component) {
  static constexpr std::string_view kShown[] = {"audio", "ip", "mcu", "runtime", "sntp", "update", "wifi"};
  return std::find(std::begin(kShown), std::end(kShown), component) != std::end(kShown);
}

void RuntimeLog::line(int channel, const char* component, std::string_view text) {
  if (!forwarded(component)) return;
  tc002::LogLine line{component, std::string(text)};
  if (live_) send(channel, line);
  else keep(std::move(line));
}

void RuntimeLog::keep(tc002::LogLine line) {
  if (backlog_.size() == kBacklog) backlog_.pop_front();
  backlog_.push_back(std::move(line));
}

// A line the runtime cannot take right away is dropped; it stays in daemon.log. When the runtime is
// gone, the line waits for the next one.
void RuntimeLog::send(int channel, const tc002::LogLine& line) {
  const std::string datagram = tc002::encodeLog(line);
  if (datagram.empty()) return;
  if (channel >= 0 &&
      ::send(channel, datagram.data(), datagram.size(), MSG_DONTWAIT | MSG_NOSIGNAL) >= 0)
    return;
  if (channel >= 0 && errno != EPIPE && errno != ECONNRESET && errno != ENOTCONN) return;
  live_ = false;
  keep(line);
}

void RuntimeLog::hello(int channel) {
  std::deque<tc002::LogLine> backlog;
  backlog.swap(backlog_);
  live_ = true;
  for (const tc002::LogLine& line : backlog) send(channel, line);
}

void RuntimeLog::readOutput(posix::UniqueFd& output, bool final) {
  char buffer[2048];
  for (int round = 0; round < 8 && output.valid(); ++round) {
    const ssize_t count = ::read(output.get(), buffer, sizeof buffer);
    if (count < 0 && errno == EINTR) continue;
    if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
    if (count <= 0) {
      output.reset();
      final = true;
      break;
    }
    outputBuffer_.append(buffer, static_cast<std::size_t>(count));
    std::size_t start = 0;
    for (;;) {
      const auto newline = outputBuffer_.find('\n', start);
      if (newline == std::string::npos) break;
      Log::text(kOutputComponent, std::string_view(outputBuffer_).substr(start, newline - start));
      start = newline + 1;
    }
    outputBuffer_.erase(0, start);
    if (outputBuffer_.size() > kMaxOutputLine) {
      Log::text(kOutputComponent, outputBuffer_);
      outputBuffer_.clear();
    }
  }
  if (final && !outputBuffer_.empty()) {
    Log::text(kOutputComponent, outputBuffer_);
    outputBuffer_.clear();
  }
}

}  // namespace awtrix::tc002d
