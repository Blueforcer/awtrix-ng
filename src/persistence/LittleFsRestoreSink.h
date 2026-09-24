#pragma once
#include <LittleFS.h>
#include "persistence/FsRestoreSink.h"

namespace awtrix::backup {
// Transport-independent device restore sink; usable by ESP32 and arduino-pico.
// One file at a time, staged alongside its destination until the complete entry lands.
class LittleFsRestoreSink final : public FsRestoreSink {
 public:
  using FsRestoreSink::FsRestoreSink;
  ~LittleFsRestoreSink() override { abortFile(); }
  bool beginFile(const std::string& path, std::string& err) override;
  bool writeFile(const uint8_t* data, std::size_t n) override;
  bool endFile() override;
  void abortFile() override;
 private:
  File file_;
  std::string path_, temp_;
  bool failed_ = false;
};
}
