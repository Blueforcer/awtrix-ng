#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace awtrix::tc002d::mcu {

struct Identity {
  uint32_t abi = 0, features = 0, family = 0, version = 0, tag = 0;
  static bool decode(const uint8_t* bytes, std::size_t length, Identity& out);
  bool sameBuild(const Identity& other) const;
};

bool readFirmwareFile(const std::string& path, std::string& data, std::size_t limit);
std::string firmwareHash(const std::string& data);
inline constexpr const char* kBaseHash = "ae9737b190501c8ac9500cae31d9fb496f33f55b8e87413dee8f4b90a9c28bbd";

// A release carries our extension and a recipe manifest. The complete image is
// assembled from a hash-pinned vendor file; persistent intent is committed before
// the OEM updater runs. An attempt the running MCU does not confirm is repeated on later
// boots, at most kMaxAttempts times in a row for one image; the journal keeps the count and
// the last failure.
class Firmware {
 public:
  static constexpr unsigned kMaxAttempts = 3;
  // Receives every status change; load() keeps it.
  void onStatus(std::function<void(const std::string&)> listener) { listener_ = std::move(listener); }
  void load(const std::string& directory, const std::string& journal);
  bool loadPrepared(const std::string& path);
  bool acceptPrepared(const std::string& data);
  bool needed(const std::string& vendorVersion, const Identity* current, bool malformed);
  bool recordAttempt();
  // The transfer or its verification failed: blocks this boot and keeps the reason in the journal.
  void attemptFailed(const std::string& reason);
  void failed(const std::string& reason);
  const std::string& status() const { return status_; }
  unsigned attempts() const { return attempts_; }
  const Identity& target() const { return target_; }
  const std::vector<uint8_t>& image() const { return image_; }
  const std::string& digest() const { return digest_; }
  const std::string& extension() const { return extension_; }
  bool valid() const { return !blocked_ && !extension_.empty(); }
  bool pending() const { return pending_; }

 private:
  void setStatus(std::string_view status);
  bool record(const char* phase, const Identity& identity, const std::string& digest, unsigned attempts,
              const std::string& error = std::string());
  unsigned nextAttempt() const;
  std::string journal_, digest_, pendingDigest_, lastError_, status_ = "unbundled";
  std::string extension_;
  uint32_t imageBytes_ = 0;
  unsigned attempts_ = 0;
  Identity target_, pendingTarget_;
  std::vector<uint8_t> image_;
  bool blocked_ = false, pending_ = false, retrying_ = false;
  std::function<void(const std::string&)> listener_;
};

}
