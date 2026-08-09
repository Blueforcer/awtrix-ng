#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace awtrix {
namespace backup {

// Highest manifest backupFormat this build understands; anything newer is refused outright.
constexpr int kBackupFormat = 1;

class RestoreSink {
 public:
  virtual ~RestoreSink() = default;

  // The apply* calls stage config; nothing is persisted until commit(), which only runs once the
  // whole archive has parsed. A failed restore must therefore leave the device untouched.
  virtual bool applyWifi(const std::string& ssid, const std::string& pass, std::string& err) = 0;
  virtual bool applySystem(const std::string& json, std::string& err) = 0;
  virtual bool applySettings(const std::string& json, std::string& err) = 0;
  virtual bool applyAppLoop(const std::string& json, std::string& err) = 0;
  virtual bool applyRadioStations(const std::string& json, std::string& err) = 0;

  virtual void commit() = 0;

  // Asset files stream straight to storage rather than being buffered, so a rejected entry has to
  // be undone with abortFile() rather than simply never written.
  virtual bool beginFile(const std::string& path, std::string& err) = 0;
  virtual bool writeFile(const uint8_t* data, std::size_t n) = 0;
  virtual bool endFile() = 0;
  virtual void abortFile() = 0;
};

struct RestoreResult {
  bool ok = false;
  std::string error;
  int wifi = 0;
  int system = 0;
  int settings = 0;
  int appLoop = 0;
  int radioStations = 0;
  int icons = 0;
  int melodies = 0;
  int palettes = 0;
  int mp3 = 0;
  int scripts = 0;
  int skipped = 0;
  std::vector<std::string> warnings;

  std::string toJson() const;
};

}
}
