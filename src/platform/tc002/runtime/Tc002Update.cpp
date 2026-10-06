#include "platform/tc002/runtime/Tc002Update.h"

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/api/ApiRouter.h"
#include "core/api/JsonReader.h"
#include "core/api/JsonWriter.h"
#include "core/render/Color.h"
#include "core/render/TextRenderer.h"
#include "platform/linux/LinuxMemory.h"
#include "platform/linux/host/vendor/httplib.h"
#include "platform/tc002/update/PackageVerifier.h"
#include "platform/tc002/update/ReleaseManifest.h"
#include "platform/tc002/update/UpdateState.h"
#include "platform/posix/Files.h"
#include "system/Log.h"

namespace awtrix {
namespace {

constexpr std::uint64_t kMultipartSlackBytes = 64ULL << 10;

bool freeBytes(int directory, std::uint64_t& out) {
  struct statvfs status {};
  if (::fstatvfs(directory, &status) != 0) return false;
  out = static_cast<std::uint64_t>(status.f_bavail) * status.f_frsize;
  return true;
}

// Bytes as megabytes with one decimal, rounded up or down.
std::string megabytes(std::uint64_t bytes, bool roundUp) {
  constexpr std::uint64_t kTenth = (1ULL << 20) / 10;
  const std::uint64_t tenths = roundUp ? (bytes + kTenth - 1) / kTenth : bytes / kTenth;
  return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10);
}

void drain(const httplib::Request& request, const httplib::ContentReader& content) {
  if (request.is_multipart_form_data())
    content([](const httplib::MultipartFormData&) { return true; }, [](const char*, std::size_t) { return true; });
  else
    content([](const char*, std::size_t) { return true; });
}

}

std::uint64_t tc002ReleaseCounter(const std::string& releaseRoot) {
  return tc002::update::readRunningRelease(releaseRoot).counter;
}

std::uint64_t tc002AcceptedCounter(const std::string& statePath, bool& readable) {
  readable = true;
  if (statePath.empty()) return 0;
  std::string state;
  if (!posix::readRegularFile(AT_FDCWD, statePath, tc002::update::kMaxStateBytes, state)) {
    readable = errno == ENOENT;
    return 0;
  }
  tc002::update::Snapshot snapshot;
  std::string error;
  readable = tc002::update::parse(state, snapshot, error);
  return readable ? snapshot.acceptedCounter : 0;
}

Tc002Update::Tc002Update(Tc002UpdateOptions options) : options_(std::move(options)) {
  packagePath_ = options_.workDirectory + "/" + kPackageName;
}

Tc002Update::~Tc002Update() {
  std::lock_guard<std::mutex> guard(mutex_);
  releaseLock(lock_);
}

void Tc002Update::releaseLock(int& lock) {
  if (lock < 0) return;
  ::flock(lock, LOCK_UN);
  ::close(lock);
  lock = -1;
}

void Tc002Update::note(std::string line) {
  std::lock_guard<std::mutex> guard(mutex_);
  if (notes_.size() < 16) notes_.push_back(std::move(line));
}

void Tc002Update::upload(const httplib::Request& request, httplib::Response& response,
                         const httplib::ContentReader& content) {
  Refusal refusal;
  bool bodyRead = false;
  if (!receive(request, content, refusal, bodyRead)) {
    if (!bodyRead) drain(request, content);
    note("update: refused - " + refusal.message);
    response.status = refusal.status;
    response.set_header("Connection", "close");
    response.set_content(api::errorJson(refusal.code, refusal.message), "application/json");
    return;
  }
  static const std::string body = "{\"ok\":true,\"applying\":true}";
  response.status = 200;
  response.set_header("Connection", "close");
  response.set_content_provider(
      body.size(), "application/json",
      [](std::size_t offset, std::size_t length, httplib::DataSink& sink) {
        return sink.write(body.data() + offset, length);
      },
      [this](bool) {
        std::lock_guard<std::mutex> guard(mutex_);
        answered_ = true;
      });
}

bool Tc002Update::receive(const httplib::Request& request, const httplib::ContentReader& content, Refusal& refusal,
                          bool& bodyRead) {
  {
    std::lock_guard<std::mutex> guard(mutex_);
    if (staged_) {
      refusal = {409, "updateBusy", "an update is already being installed"};
      return false;
    }
    if (statusKnown_ && status_.state == "boot-pending") {
      refusal = {409, "updateBusy", "the last update is still being confirmed - try again in a few minutes"};
      return false;
    }
  }
  if (!request.is_multipart_form_data()) {
    refusal = {400, "badRequest", "send the package as the multipart field \"firmware\""};
    return false;
  }
  const std::uint64_t declared =
      request.has_header("Transfer-Encoding") ? 0 : request.get_header_value_u64("Content-Length");
  if (declared > kMaxRequestBytes) {
    refusal = {413, "payloadTooLarge", "the update package is larger than 8 MB"};
    return false;
  }
  const int directory = posix::openPrivateDirectoryAt(AT_FDCWD, options_.workDirectory, true).release();
  if (directory < 0) {
    refusal = {500, "internalError", "cannot prepare the update directory"};
    return false;
  }
  int lock = ::openat(directory, "lock", O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
  if (lock < 0 || ::flock(lock, LOCK_EX | LOCK_NB) != 0) {
    const bool busy = lock >= 0 && errno == EWOULDBLOCK;
    if (lock >= 0) ::close(lock);
    ::close(directory);
    refusal = busy ? Refusal{409, "updateBusy", "another update is being uploaded or installed"}
                   : Refusal{500, "internalError", "cannot lock the update directory"};
    return false;
  }
  {
    std::lock_guard<std::mutex> guard(mutex_);
    error_.clear();
  }
  ::unlinkat(directory, kPackageName, 0);
  std::uint64_t room = 0;
  if (!freeBytes(directory, room)) room = 0;
  std::uint64_t memory = UINT64_MAX;
  readMemAvailable(memory);
  room = std::min(room, memory);
  const std::uint64_t limit = room > kRamReserveBytes ? std::min(kMaxPackageBytes, room - kRamReserveBytes) : 0;
  const auto fail = [&](Refusal why) {
    ::unlinkat(directory, kPackageName, 0);
    ::close(directory);
    releaseLock(lock);
    refusal = std::move(why);
    return false;
  };
  const auto memoryRefusal = [&](std::uint64_t size) {
    return Refusal{413, "insufficientMemory", "not enough memory for the package: need " +
                                                  megabytes(size + kRamReserveBytes, true) + " MB, free " +
                                                  megabytes(room, false) + " MB - restart the clock and try again"};
  };
  if (declared > limit + kMultipartSlackBytes) return fail(memoryRefusal(declared - kMultipartSlackBytes));

  enum class Problem { None, Field, TooLarge, Storage };
  Problem problem = Problem::None;
  int file = -1;
  bool seen = false;
  std::uint64_t written = 0;
  bodyRead = true;
  const bool read = content(
      [&](const httplib::MultipartFormData& part) {
        if (seen || part.name != "firmware" || part.filename.empty()) {
          problem = Problem::Field;
          return false;
        }
        seen = true;
        file = ::openat(directory, kPackageName, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (file < 0) problem = Problem::Storage;
        return file >= 0;
      },
      [&](const char* data, std::size_t size) {
        if (file < 0) {
          problem = Problem::Field;
          return false;
        }
        if (size > limit - written) {
          problem = Problem::TooLarge;
          return false;
        }
        if (!posix::writeAll(file, data, size)) {
          problem = errno == ENOSPC ? Problem::TooLarge : Problem::Storage;
          return false;
        }
        written += size;
        return true;
      });
  if (file >= 0 && ::close(file) != 0 && problem == Problem::None) problem = Problem::Storage;
  switch (problem) {
    case Problem::Field:
      return fail({400, "badRequest", "send exactly one file, as the multipart field \"firmware\""});
    case Problem::TooLarge:
      return fail(limit < kMaxPackageBytes ? memoryRefusal(written)
                                           : Refusal{413, "payloadTooLarge", "the update package is larger than 8 MB"});
    case Problem::Storage:
      return fail({500, "internalError", "cannot store the uploaded package"});
    case Problem::None:
      break;
  }
  if (!read || !seen || written == 0)
    return fail({400, "badRequest", seen ? "the upload ended before the package was complete"
                                         : "no file in the multipart field \"firmware\""});
  tc002::UpdateReady ready;
  if (!verifyPackage(refusal, ready)) return fail(std::move(refusal));
  ::close(directory);
  std::lock_guard<std::mutex> guard(mutex_);
  lock_ = lock;
  ready_ = std::move(ready);
  staged_ = true;
  answered_ = sent_ = false;
  notes_.push_back("update: " + ready_.release + " verified, handing it to the supervisor");
  return true;
}

bool Tc002Update::verifyPackage(Refusal& refusal, tc002::UpdateReady& ready) {
  bool stateReadable = true;
  const std::uint64_t accepted =
      std::max(tc002AcceptedCounter(options_.statePath, stateReadable), tc002ReleaseCounter(options_.releaseRoot));
  if (!stateReadable) note("update: " + options_.statePath + " is unreadable; the release counter applies");
  tc002::update::Policy policy;
  policy.expectedTarget = tc002::update::kProductionTarget;
  policy.currentCounter = accepted;
  policy.maxPayloadBytes = kMaxPackageBytes;
  const tc002::update::Result result = tc002::update::verify(packagePath_, policy);
  if (!result.ok) {
    const std::string& code = result.code;
    if (code == "counter")
      refusal = {409, "notNewer", "this package is not newer than the installed firmware"};
    else if (code == "digest")
      refusal = {400, "invalidPackage", "the package is damaged or incomplete"};
    else if (code == "target")
      refusal = {400, "wrongTarget", "this package is not for the TC002"};
    else if (code == "format" || code == "limit" || code == "path")
      refusal = {400, "invalidPackage", "not an AWTRIX NG TC002 update package (" + result.error + ")"};
    else
      refusal = {500, "internalError", result.error};
    return false;
  }
  std::uint64_t capacity = 0;
  {
    std::lock_guard<std::mutex> guard(mutex_);
    capacity = statusKnown_ ? status_.capacity : 0;
  }
  if (capacity == 0) {
    refusal = {500, "internalError", "the supervisor did not report the release slot"};
    return false;
  }
  if (result.payloadBytes > capacity) {
    refusal = {409, "insufficientStorage", "the firmware image takes " + megabytes(result.payloadBytes, true) +
                                                 " MB, the clock holds " + megabytes(capacity, false) + " MB"};
    return false;
  }
  ready = {packagePath_, result.release, result.counter, result.payloadSha256};
  return true;
}

bool Tc002Update::ownsPanel() const {
  std::lock_guard<std::mutex> guard(mutex_);
  return staged_;
}

void Tc002Update::draw(Canvas& canvas, const GfxFont& font) const {
  canvas.clear(color::kBlack);
  text::drawCentered(canvas, font, "UPDATE", (canvas.height() - 8) / 2 + 6, 0xFFFFFFu);
}

std::string Tc002Update::poll(int64_t nowMs, bool frameShown) {
  std::vector<std::string> notes;
  std::string datagram;
  {
    std::lock_guard<std::mutex> guard(mutex_);
    notes.swap(notes_);
    if (staged_ && answered_ && frameShown && !sent_) {
      sent_ = true;
      sentAtMs_ = nowMs;
      datagram = tc002::encodeUpdateReady(ready_);
    } else if (staged_ && sent_ && nowMs - sentAtMs_ > kHandoffTimeoutMs) {
      ::unlink(packagePath_.c_str());
      releaseLock(lock_);
      staged_ = answered_ = sent_ = false;
      error_ = "the supervisor did not install the update";
      notes.push_back("update: " + error_);
    }
  }
  for (const auto& line : notes) logf("%s", line.c_str());
  return datagram;
}

void Tc002Update::applyHello(const tc002::SupervisorMessage& hello) {
  std::lock_guard<std::mutex> guard(mutex_);
  if (!hello.hasUpdate) return;
  statusKnown_ = true;
  status_ = hello.update;
  const std::string refused = "update " + ready_.release + " refused: ";
  if (staged_ && sent_ && status_.error.compare(0, refused.size(), refused) == 0) {
    ::unlink(packagePath_.c_str());
    releaseLock(lock_);
    staged_ = answered_ = sent_ = false;
    error_ = status_.error;
    if (notes_.size() < 16) notes_.push_back("update: " + error_);
  }
}

void Tc002Update::addFacts(DeviceFacts& facts) const { facts.updateImage = kImageName; }

void Tc002Update::writeMembers(api::JsonWriter& json) const {
  std::lock_guard<std::mutex> guard(mutex_);
  json.key("update").beginObject();
  if (staged_) {
    json.member("state", "applying").member("release", ready_.release).member("error", "");
  } else {
    json.member("state", statusKnown_ ? status_.state : std::string("idle"))
        .member("release", statusKnown_ ? status_.release : std::string())
        .member("error", !error_.empty() ? error_ : statusKnown_ ? status_.error : std::string());
  }
  json.endObject();
}

}
