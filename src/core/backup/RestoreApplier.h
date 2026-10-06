#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "core/backup/Restore.h"
#include "core/backup/ZipReader.h"
#include "core/assets/AssetContent.h"

namespace awtrix {
namespace backup {

// Large enough for all 32 radio stations even with every character JSON-escaped. Backups
// stream assets; these limits apply only to the metadata records that must fit in memory.
inline constexpr std::size_t kMaxRestoreMetadataBytes = 64 * 1024;
inline constexpr std::size_t kMaxRestoreManifestBytes = 4 * 1024;
inline constexpr std::size_t kMaxRestoreWarnings = 128;

class RestoreApplier : public ZipVisitor {
 public:
  explicit RestoreApplier(RestoreSink& sink);
  ~RestoreApplier() override;
  RestoreApplier(const RestoreApplier&) = delete;
  RestoreApplier& operator=(const RestoreApplier&) = delete;

  const RestoreResult& result() const { return result_; }
  // Cancels pending configuration and releases an unfinished file; completed entries stay restored.
  void abort(const std::string& reason);

  void onEntryStart(const std::string& name, uint32_t size) override;
  void onEntryData(const uint8_t* data, std::size_t n) override;
  void onEntryEnd(bool crcOk) override;
  void onArchiveEnd() override;

 private:
  enum class Kind {
    Manifest, Wifi, System, Settings, AppLoop, RadioStations,
    IconOrigins, Icon, Melody, Palette, Mp3, Script, Unknown
  };

  static Kind classify(const std::string& name);
  void fail(const std::string& message);
  // Counts the entry as skipped and records why.
  void skip(std::string message);

  RestoreSink& sink_;
  RestoreResult result_;
  bool manifestOk_ = false;
  bool fatal_ = false;

  Kind kind_ = Kind::Unknown;
  std::string name_;
  bool buffering_ = false;
  std::string buf_;
  std::string pendingIconOrigins_;
  bool metadataTooLarge_ = false;
  std::size_t metadataLimit_ = kMaxRestoreMetadataBytes;
  bool fileOpen_ = false;
  bool fileRejected_ = false;
  bool validateContent_ = false;
  assets::UploadValidator contentValidator_;
  bool completed_ = false;
};

}
}
