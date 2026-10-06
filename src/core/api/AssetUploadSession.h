#pragma once

#include <functional>
#include <string>

#include "core/api/AssetApi.h"

namespace awtrix::api::asset {

struct StreamStorage {
  // begin creates a private staging file; publish replaces the destination only after the
  // complete content passes validation. discard must preserve the old destination.
  std::function<bool(const std::string&)> exists;
  // created is true only when this call created the directory.
  std::function<bool(const std::string&, bool& created)> mkdir;
  std::function<void(const std::string&)> prune;
  std::function<bool(const std::string&)> begin;
  std::function<bool(const uint8_t*, std::size_t)> write;
  std::function<bool(const std::string&)> publish;
  std::function<void()> discard;
  std::function<void()> changed;
};

// A multipart request may contain several files. Each file publishes independently, but a
// failed part makes the response fail and prevents later parts from hiding that failure.
class UploadSession {
 public:
  UploadSession() = default;
  UploadSession(const UploadSession&) = delete;
  UploadSession& operator=(const UploadSession&) = delete;
  ~UploadSession() { discard(); }
  void reset(StreamStorage storage);
  void authorize(const HttpResult& policy);
  void start(const std::string& directory, const std::string& filename);
  // A target another route resolved, such as a sound in a script's folder.
  void start(const UploadTarget& target);
  void append(const uint8_t* data, std::size_t size);
  void end();
  void abort();
  HttpResult complete();

 private:
  void discard();
  void fail(HttpResult result);
  StreamStorage storage_;
  UploadValidator validator_;
  std::string path_;
  std::string directory_;
  HttpResult failure_;
  bool known_ = false;
  bool authorized_ = false;
  bool failed_ = false;
  bool received_ = false;
  bool active_ = false;
};

}
