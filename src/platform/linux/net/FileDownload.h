#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace awtrix::net {
// Fetches one HTTP(S) resource into a private temporary file on its own thread. start() and
// cancel() drop the download before without waiting for the network; a result only ever belongs
// to the generation start() returned.
class FileDownload {
 public:
  struct Limits {
    std::size_t maxBytes = 0;
    // From connecting to the last byte, redirects included.
    int64_t timeoutMs = 0;
    bool followRedirects = false;
    // Whether more bytes still fit next to what is written: asked once with the size
    // Content-Length announces, then before every block. false stops the download; empty: no
    // check. Called on the download's thread.
    std::function<bool(std::size_t more)> room;
  };
  enum class Failure : uint8_t { None, Network, Status, TooLarge, NoRoom, Empty };
  struct Result {
    uint64_t generation = 0;
    std::string path;
    Failure failure = Failure::None;
    int status = 0;
    bool success() const { return failure == Failure::None; }
  };
  // Files go to a private folder "<directory>/<prefix>-XXXXXX", removed again with this object.
  FileDownload(const std::string& directory, const std::string& prefix, Limits limits);
  ~FileDownload();
  // origin is "http(s)://host[:port]", target the path and query.
  uint64_t start(std::string origin, std::string target);
  void cancel();
  // A finished download; its file belongs to the caller from here on.
  bool poll(Result& result);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace awtrix::net
