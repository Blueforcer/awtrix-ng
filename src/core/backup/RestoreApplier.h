#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "core/backup/Restore.h"
#include "core/backup/ZipReader.h"

namespace awtrix {
namespace backup {

class RestoreApplier : public ZipVisitor {
 public:
  explicit RestoreApplier(RestoreSink& sink);

  const RestoreResult& result() const { return result_; }

  void onEntryStart(const std::string& name, uint32_t size) override;
  void onEntryData(const uint8_t* data, std::size_t n) override;
  void onEntryEnd(bool crcOk) override;
  void onArchiveEnd() override;

 private:
  enum class Kind {
    Manifest, Wifi, System, Settings, AppLoop, RadioStations,
    Icon, Melody, Palette, Clip, Script, Unknown
  };

  static Kind classify(const std::string& name);
  void fail(const std::string& message);

  RestoreSink& sink_;
  RestoreResult result_;
  bool manifestOk_ = false;
  bool fatal_ = false;

  Kind kind_ = Kind::Unknown;
  std::string name_;
  bool buffering_ = false;
  std::string buf_;
  bool fileOpen_ = false;
  bool fileRejected_ = false;
  bool contentChecked_ = false;
};

}
}
