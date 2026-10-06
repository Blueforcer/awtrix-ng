#include "platform/linux/net/FileDownload.h"

#include <unistd.h>

#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <thread>

#include "platform/linux/host/vendor/httplib.h"
#include "platform/linux/net/HttpClient.h"
#include "platform/posix/Files.h"

namespace awtrix::net {
struct FileDownload::Impl {
  Limits limits;
  std::string directory, origin, target;
  std::mutex mutex;
  std::condition_variable changed;
  std::thread thread;
  bool quit = false, pending = false, ready = false;
  uint64_t generation = 0;
  Result result;
  SocketInterrupt interrupt;
  Impl(const std::string& dir, const std::string& prefix, Limits l) : limits(std::move(l)) {
    std::string pattern = dir + "/" + prefix + "-XXXXXX";
    if (::mkdtemp(pattern.data())) directory = std::move(pattern);
    thread = std::thread([this] { run(); });
  }
  ~Impl() {
    {
      std::lock_guard<std::mutex> lock(mutex);
      quit = true;
      ++generation;
      interrupt.interrupt();
    }
    changed.notify_one();
    thread.join();
    if (!result.path.empty()) ::unlink(result.path.c_str());
    if (!directory.empty()) ::rmdir(directory.c_str());
  }
  bool current(uint64_t g) {
    std::lock_guard<std::mutex> lock(mutex);
    return !quit && generation == g;
  }
  void run() {
    for (;;) {
      std::unique_lock<std::mutex> lock(mutex);
      changed.wait(lock, [&] { return quit || pending; });
      if (quit) return;
      const uint64_t g = generation;
      const std::string path = target;
      httplib::Client connection(origin);
      pending = false;
      // A cancel from here on interrupts this download: it moves the generation on under the
      // same lock.
      interrupt.rearm();
      lock.unlock();
      configure(connection, {limits.timeoutMs, 5, limits.followRedirects});
      interrupt.watch(connection);
      std::string file = directory + "/file-XXXXXX";
      int fd = directory.empty() ? -1 : ::mkstemp(file.data());
      Failure failure = Failure::Network;
      int status = 0;
      if (fd >= 0 && current(g)) {
        ReceivedBody received;
        bool noRoom = false;
        const BodyPolicy policy{limits.maxBytes, [&] { return current(g); },
                                [&](std::size_t more) {
          noRoom = limits.room && !limits.room(more);
          return !noRoom;
        }};
        auto response = receive200(connection, path, policy, received,
            [&](const char* bytes, std::size_t count) {
              return posix::writeAll(fd, bytes, count);
            });
        status = received.status;
        if (received.tooLarge) failure = Failure::TooLarge;
        else if (noRoom) failure = Failure::NoRoom;
        else if (status != 200 && status != 0) failure = Failure::Status;
        else if (response && response->status == 200)
          failure = received.size > 0 ? Failure::None : Failure::Empty;
      }
      if (fd >= 0) ::close(fd);
      interrupt.release();
      lock.lock();
      bool kept = false;
      if (generation == g && !quit) {
        if (!result.path.empty()) ::unlink(result.path.c_str());
        kept = failure == Failure::None;
        result = {g, kept ? file : "", failure, status};
        ready = true;
      }
      lock.unlock();
      if (!kept && fd >= 0) ::unlink(file.c_str());
    }
  }
};
FileDownload::FileDownload(const std::string& directory, const std::string& prefix, Limits limits)
    : impl_(new Impl(directory, prefix, std::move(limits))) {}
FileDownload::~FileDownload() = default;
uint64_t FileDownload::start(std::string origin, std::string target) {
  cancel();
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->origin = std::move(origin);
  impl_->target = std::move(target);
  impl_->pending = true;
  impl_->changed.notify_one();
  return impl_->generation;
}
void FileDownload::cancel() {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  ++impl_->generation;
  impl_->pending = impl_->ready = false;
  impl_->interrupt.interrupt();
  if (!impl_->result.path.empty()) ::unlink(impl_->result.path.c_str());
  impl_->result = {};
}
bool FileDownload::poll(Result& result) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (!impl_->ready) return false;
  result = std::move(impl_->result);
  impl_->result = {};
  impl_->ready = false;
  return true;
}
}  // namespace awtrix::net
