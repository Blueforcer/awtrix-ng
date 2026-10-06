#include "platform/tc002/daemon/update/UpdateService.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "core/api/JsonReader.h"
#include "platform/posix/Files.h"
#include "platform/posix/Text.h"
#include "platform/tc002/daemon/Log.h"
#include "platform/tc002/update/ReleaseManifest.h"

namespace awtrix {
namespace tc002d {

using tc002::validReleaseName;
using tc002::update::kProductionTarget;

namespace {

constexpr std::size_t kMaxHelperBytes = 1 << 20;

// A private copy in RAM: the helper replaces the release it comes from, so it must not run from it.
bool copyHelper(const std::string& from, const std::string& to, std::string& error) {
  posix::UniqueFd source(::open(from.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
  struct stat info{};
  if (!source.valid() || ::fstat(source.get(), &info) != 0 || !S_ISREG(info.st_mode) || info.st_size <= 0 ||
      static_cast<uint64_t>(info.st_size) > kMaxHelperBytes) {
    error = "the release has no flash helper at " + from;
    return false;
  }
  std::string bytes(static_cast<std::size_t>(info.st_size), '\0');
  if (!posix::readAll(source.get(), bytes.data(), bytes.size())) {
    error = "cannot read " + from;
    return false;
  }
  ::unlink(to.c_str());
  posix::UniqueFd target(::open(to.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0700));
  if (!target.valid() || !posix::writeAll(target.get(), bytes.data(), bytes.size())) {
    error = "cannot copy the flash helper to " + to + ": " + std::strerror(errno);
    ::unlink(to.c_str());
    return false;
  }
  return true;
}

}

UpdateService::UpdateService(UpdateServiceOptions options, UpdateLinks links)
    : options_(std::move(options)),
      links_(std::move(links)),
      record_(options_.paths.stateDir(), options_.running.counter) {
  refreshStatus();
}

void UpdateService::refreshStatus() {
  std::string error;
  status_ = record_.status(&error);
  status_.capacity = options_.capacity;
  if (!error.empty()) Log::line("update", "%s", error.c_str());
}

bool UpdateService::start(int64_t nowMs) {
  if (!options_.confirmPending) return true;
  confirming_ = true;
  nextCheck_ = nowMs;
  Log::line("update", "release %s is new: confirming once the runtime is healthy", options_.candidate.c_str());
  return true;
}

void UpdateService::requestStop(int64_t nowMs) {
  (void)nowMs;
  if (confirming_) Log::line("update", "stopping before release %s was confirmed", options_.candidate.c_str());
  confirming_ = false;
}

int64_t UpdateService::nextDeadlineMs() const { return confirming_ ? nextCheck_ : -1; }

void UpdateService::onTime(int64_t nowMs) {
  if (!confirming_ || nowMs < nextCheck_) return;
  if (links_.runtimeHealthy && links_.runtimeHealthy()) {
    confirmed();
    return;
  }
  nextCheck_ = nowMs + options_.checkIntervalMs;
}

void UpdateService::confirmed() {
  confirming_ = false;
  std::string error;
  if (!record_.confirm(error)) {
    Log::line("update", "cannot confirm release %s: %s", options_.candidate.c_str(), error.c_str());
    refreshStatus();
    return;
  }
  Log::line("update", "release %s confirmed", options_.candidate.c_str());
  for (const std::string& path : options_.bootAttemptsPaths) {
    if (::unlink(path.c_str()) == 0) {
      if (!posix::fsyncDirectory(posix::parentDirectory(path)))
        Log::line("update", "cleared %s, but its directory sync failed", path.c_str());
    } else if (errno != ENOENT) {
      Log::line("update", "cannot clear the loader start counter %s: %s", path.c_str(), std::strerror(errno));
    }
  }
  refreshStatus();
  if (links_.statusChanged) links_.statusChanged();
}

bool UpdateService::handOff(const tc002::UpdateReady& ready) {
  const auto refuse = [&](const std::string& reason) {
    Log::line("update", "update %s refused: %s", ready.release.c_str(), reason.c_str());
    refreshStatus();
    status_.error = posix::printable("update " + ready.release + " refused: " + reason, 256);
    if (links_.statusChanged) links_.statusChanged();
    return false;
  };
  if (handOff_) return refuse("another update is being installed");
  if (confirming_) return refuse("the running release is not confirmed yet");
  if (options_.capacity == 0) return refuse("the clock reports no release slot; update over USB");
  if (links_.mcuFirmwareIdle && !links_.mcuFirmwareIdle()) return refuse("MCU firmware update is in progress");
  const std::string& directory = options_.paths.updateDir;
  const std::string name = ready.package.size() > directory.size() + 1
                               ? ready.package.substr(directory.size() + 1) : std::string();
  if (ready.package.compare(0, directory.size() + 1, directory + "/") != 0 || name.empty() || name == "." ||
      name == ".." || name.find('/') != std::string::npos || name == kFlashHelper)
    return refuse("the package is not in " + directory);
  struct stat info{};
  if (::lstat(directory.c_str(), &info) != 0 || !S_ISDIR(info.st_mode) || info.st_uid != ::geteuid() ||
      (info.st_mode & 077) != 0)
    return refuse(directory + " is not a private directory");
  posix::UniqueFd package(::open(ready.package.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK));
  if (!package.valid() || ::fstat(package.get(), &info) != 0 || !S_ISREG(info.st_mode) ||
      info.st_uid != ::geteuid())
    return refuse("the package is not a regular file of this user");
  PackageHeader header;
  std::string error;
  if (!readPackageHeader(package.get(), static_cast<uint64_t>(info.st_size), header, error)) return refuse(error);
  if (header.target != kProductionTarget || header.release != ready.release || header.counter != ready.counter ||
      header.payloadSha256 != ready.payloadSha256 || !validReleaseName(header.release))
    return refuse("the package does not match the hand-off");
  if (header.payloadBytes > options_.capacity) return refuse("the release image does not fit the release slot");
  const std::string helper = directory + "/" + kFlashHelper;
  if (!copyHelper(options_.root + "/bin/" + kFlashHelper, helper, error)) return refuse(error);
  ::unlink(options_.paths.updateResult().c_str());
  if (!record_.stage(header, ready.package, error)) {
    ::unlink(helper.c_str());
    return refuse(error);
  }
  handOff_ = true;
  package_ = ready.package;
  header_ = header;
  helper_ = helper;
  status_ = {"applying", header.release, "", options_.capacity};
  Log::line("update", "update %s (counter %llu) staged; stopping everything to write the release slot",
            header.release.c_str(), static_cast<unsigned long long>(header.counter));
  if (links_.stop) links_.stop("installing update " + header.release);
  return true;
}

bool UpdateService::prepareExec(ExecPlan& plan, std::string& error) {
  StagedUpdate staged;
  const bool pending = record_.begin(staged, error) && record_.bootPending(error);
  record_.close();
  if (!pending) return false;
  plan.executable = helper_;
  plan.arguments = {helper_, "write-slot", package_,
                    "--offset", std::to_string(header_.payloadOffset),
                    "--length", std::to_string(header_.payloadBytes),
                    "--sha256", header_.payloadSha256,
                    "--release", header_.release,
                    "--counter", std::to_string(header_.counter),
                    "--mount", options_.root,
                    "--result", options_.paths.updateResult(),
                    "--reboot"};
  return true;
}

void UpdateService::execFailed(const std::string& reason) {
  std::string error;
  if (!record_.fail(header_.release, reason, error)) Log::line("update", "cannot record the failure: %s", error.c_str());
  handOff_ = false;
  refreshStatus();
}

void UpdateService::appendStatus(api::JsonWriter& json) const {
  json.key("update").beginObject()
      .member("state", status_.state)
      .member("release", status_.release)
      .member("error", status_.error)
      .member("capacity", static_cast<long long>(options_.capacity))
      .member("confirming", confirming_)
      .member("handOff", handOff_)
      .endObject();
}

std::string UpdateService::helloDatagram() const {
  std::string hello = tc002::encodeHello(AWTRIX_NG_VERSION, status_);
  if (hello.empty()) hello = tc002::encodeHello(AWTRIX_NG_VERSION, tc002::UpdateStatus{"idle", "", "", options_.capacity});
  return hello;
}

}
}
