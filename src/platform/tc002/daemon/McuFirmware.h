#pragma once

#include <cstdint>
#include <string>
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
// the OEM updater runs.
class Firmware {
 public:
  void load(const std::string& directory, const std::string& journal);
  bool loadPrepared(const std::string& path);
  bool acceptPrepared(const std::string& data);
  bool needed(const std::string& vendorVersion, const Identity* current, bool malformed);
  bool recordAttempt();
  void failed(const std::string& reason);
  const std::string& status() const { return status_; }
  const Identity& target() const { return target_; }
  const std::vector<uint8_t>& image() const { return image_; }
  const std::string& digest() const { return digest_; }
  const std::string& extension() const { return extension_; }
  bool valid() const { return !blocked_ && !extension_.empty(); }
  bool pending() const { return pending_; }

 private:
  bool record(const char* phase, const Identity& identity, const std::string& digest);
  std::string journal_, digest_, pendingDigest_, status_ = "unbundled";
  std::string extension_;
  uint32_t imageBytes_ = 0;
  Identity target_, pendingTarget_;
  std::vector<uint8_t> image_;
  bool blocked_ = false, pending_ = false;
};

}
