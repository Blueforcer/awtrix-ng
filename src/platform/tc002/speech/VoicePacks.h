#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include "platform/tc002/speech/SpeechModelVoice.h"

namespace httplib {
struct Request;
struct Response;
}

namespace awtrix::speech {

// The clock's voice: the release's pack, or the one pack the owner uploaded in its place.
// GET, POST and DELETE /api/v1/speech/voice show, replace and remove the uploaded one; the new voice
// speaks from the next speech on.
class VoicePacks {
 public:
  using Apply = std::function<void(std::shared_ptr<SpeechVoice>)>;
  // builtinPath is the release's pack, userPath where an upload is kept; apply hands the voice on.
  VoicePacks(std::string builtinPath, std::string userPath, uint64_t reserveBytes, Apply apply);

  // The uploaded pack, else the release's; logs a pack it refuses. False when neither loads.
  bool load();
  bool handle(const httplib::Request& request, httplib::Response& response);

  static constexpr std::size_t kMaxPackBytes = 2000000;

 private:
  struct Active {
    std::shared_ptr<ModelVoice> voice;
    std::string name, language, version;
    std::size_t bytes = 0;
    bool uploaded = false;
  };

  static bool describe(std::shared_ptr<const SpeechModel> model, bool uploaded, std::size_t bytes, Active& out,
                       std::string& error);
  bool open(const std::string& path, bool uploaded, Active& out, std::string& error) const;
  bool install(const std::string& data, int& status, std::string& error);
  bool remove(std::string& error);
  std::string json() const;
  void activate(Active next);

  const std::string builtinPath_, userPath_;
  const uint64_t reserveBytes_;
  const Apply apply_;
  mutable std::mutex mutex_;
  std::mutex changeMutex_;  // one POST or DELETE at a time
  Active active_;
};

}
