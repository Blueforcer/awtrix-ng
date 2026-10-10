#include "platform/tc002/speech/VoicePacks.h"

#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "core/api/ApiRouter.h"
#include "core/api/JsonReader.h"
#include "core/api/JsonWriter.h"
#include "platform/linux/host/vendor/httplib.h"
#include "platform/posix/Files.h"
#include "system/Log.h"

namespace awtrix::speech {
namespace {

constexpr const char* kPath = "/api/v1/speech/voice";
constexpr std::size_t kMaxInfoText = 64;

void sendError(httplib::Response& res, int status, const char* code, const std::string& message) {
  res.status = status;
  res.set_content(api::errorJson(code, message, {}), "application/json");
}

bool infoField(const api::JsonReader& info, const char* name, bool required, std::string& out) {
  const auto member = api::memberValue(info, name);
  if (!api::present(member)) return !required;
  out.clear();
  return member.appendString(out) && !out.empty() && out.size() <= kMaxInfoText;
}

std::string directoryOf(const std::string& path) {
  const auto slash = path.rfind('/');
  return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}

uint64_t fileBytes(const std::string& path) {
  struct stat info{};
  return ::stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode) ? static_cast<uint64_t>(info.st_size) : 0;
}

}

VoicePacks::VoicePacks(std::string builtinPath, std::string userPath, uint64_t reserveBytes, Apply apply)
    : builtinPath_(std::move(builtinPath)), userPath_(std::move(userPath)), reserveBytes_(reserveBytes),
      apply_(std::move(apply)) {}

bool VoicePacks::describe(std::shared_ptr<const SpeechModel> model, bool uploaded, std::size_t bytes, Active& out,
                          std::string& error) {
  auto voice = ModelVoice::create(model, error);
  if (!voice) return false;
  if (!api::isWellFormed(model->info)) {
    error = "pack.info: not JSON";
    return false;
  }
  const api::JsonReader info(model->info);
  Active next;
  if (!info.isObject() || !infoField(info, "name", true, next.name) ||
      !infoField(info, "language", true, next.language) || !infoField(info, "version", false, next.version)) {
    error = "pack.info: name and language are required, at most 64 bytes each";
    return false;
  }
  next.voice = std::move(voice);
  next.bytes = bytes;
  next.uploaded = uploaded;
  out = std::move(next);
  return true;
}

bool VoicePacks::open(const std::string& path, bool uploaded, Active& out, std::string& error) const {
  auto model = SpeechModel::load(path, error);
  return model && describe(std::move(model), uploaded, static_cast<std::size_t>(fileBytes(path)), out, error);
}

void VoicePacks::activate(Active next) {
  std::shared_ptr<SpeechVoice> voice = next.voice;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    active_ = std::move(next);
  }
  apply_(std::move(voice));
}

bool VoicePacks::load() {
  Active next;
  std::string error;
  if (fileBytes(userPath_)) {
    if (open(userPath_, true, next, error)) {
      activate(std::move(next));
      return true;
    }
    logf("speech: %s: %s; using the release's voice", userPath_.c_str(), error.c_str());
  }
  if (open(builtinPath_, false, next, error)) {
    activate(std::move(next));
    return true;
  }
  logf("speech: %s: %s; no voice", builtinPath_.c_str(), error.c_str());
  activate({});
  return false;
}

bool VoicePacks::install(const std::string& data, int& status, std::string& error) {
  status = 400;
  if (data.empty() || data.size() > kMaxPackBytes) {
    error = "a voice pack has 1.." + std::to_string(kMaxPackBytes) + " bytes";
    return false;
  }
  auto model = SpeechModel::parse(reinterpret_cast<const uint8_t*>(data.data()), data.size(), error);
  Active next;
  if (!model || !describe(std::move(model), true, data.size(), next, error)) return false;
  const std::string directory = directoryOf(userPath_);
  if (!posix::ensurePrivateDirectory(directory)) {
    status = 500;
    error = std::string("cannot create ") + directory + ": " + std::strerror(errno);
    return false;
  }
  struct statvfs fs{};
  if (::statvfs(directory.c_str(), &fs) == 0) {
    const uint64_t room = uint64_t{fs.f_bavail} * fs.f_frsize + fileBytes(userPath_);
    if (room < data.size() + reserveBytes_) {
      status = 507;
      error = "not enough free storage";
      return false;
    }
  }
  if (!posix::replaceText(userPath_, data)) {
    status = 507;
    error = std::string("write failed: ") + std::strerror(errno);
    return false;
  }
  activate(std::move(next));
  return true;
}

bool VoicePacks::remove(std::string& error) {
  if (::unlink(userPath_.c_str()) != 0 && errno != ENOENT) {
    error = std::string("remove failed: ") + std::strerror(errno);
    return false;
  }
  load();
  return true;
}

std::string VoicePacks::json() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::string out;
  api::JsonWriter w(out);
  w.beginObject();
  if (active_.voice) {
    w.member("name", active_.name)
        .member("language", active_.language)
        .member("version", active_.version)
        .member("bytes", static_cast<unsigned long long>(active_.bytes))
        .member("uploaded", active_.uploaded);
  } else {
    w.memberNull("name");
  }
  w.member("maxBytes", static_cast<unsigned long long>(kMaxPackBytes)).endObject();
  return out;
}

bool VoicePacks::handle(const httplib::Request& req, httplib::Response& res) {
  if (req.path != kPath) return false;
  res.set_header("Cache-Control", "no-store");
  if (req.method == "GET") {
    res.set_content(json(), "application/json");
  } else if (req.method == "POST") {
    std::lock_guard<std::mutex> lock(changeMutex_);
    const std::string* data = &req.body;
    if (req.is_multipart_form_data()) {
      data = nullptr;
      for (const auto& file : req.files)
        if (!file.second.filename.empty()) {
          data = &file.second.content;
          break;
        }
      if (!data) {
        sendError(res, 400, "validationFailed", "no file in the form");
        return true;
      }
    }
    int status = 0;
    std::string error;
    if (!install(*data, status, error)) {
      sendError(res, status, status == 507 ? "insufficientStorage" : status == 400 ? "validationFailed"
                                                                                    : "internalError",
                error);
      return true;
    }
    logf("speech: voice pack installed, %zu bytes", data->size());
    res.set_content(json(), "application/json");
  } else if (req.method == "DELETE") {
    std::lock_guard<std::mutex> lock(changeMutex_);
    std::string error;
    if (!remove(error)) {
      sendError(res, 500, "internalError", error);
      return true;
    }
    res.set_content(json(), "application/json");
  } else {
    sendError(res, 405, "methodNotAllowed", "allowed: GET, POST, DELETE");
  }
  return true;
}

}
