#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "core/AssetPaths.h"
#include "core/api/ApiRouter.h"
#include "core/assets/AssetContent.h"

namespace awtrix::iconorigins { class Backend; }

namespace awtrix::api::asset {

// The adapter owns filesystem durability and confinement. write() must report success only
// after all bytes are stored; the service announces an asset change only after that succeeds.
class Backend {
 public:
  virtual ~Backend() = default;
  virtual bool exists(const std::string& path) = 0;
  virtual bool write(const std::string& path, const std::string& content) = 0;
  virtual bool remove(const std::string& path) = 0;
  virtual bool rename(const std::string& from, const std::string& to) = 0;
  virtual void release(const std::string& path) = 0;
  virtual void changed() = 0;
  iconorigins::Backend* iconOrigins = nullptr;
};

// 409 nameTaken when the other kind of stored sound already has path's name (see
// sound::namesakePath); 200 otherwise, and without asking exists for a path that has no namesake.
HttpResult claimName(const std::string& path,
                     const std::function<bool(const std::string&)>& exists);

HttpResult writeMelody(const std::string& name, const std::string& body, Backend& storage);
HttpResult deleteMelody(const std::string& name, Backend& storage);
HttpResult deleteFile(const std::string& path, Backend& storage);
HttpResult deleteMp3(const std::string& name, Backend& storage);
// {"from":"sun.gif","to":"sunny.gif"}: same extension, a name no icon has, the Hub link moves along.
HttpResult renameIcon(const std::string& body, Backend& storage);
// {"from":"ding","to":"bell"}: names without ".mp3"; refused when an MP3 or a melody has "to".
HttpResult renameMp3(const std::string& body, Backend& storage);

// What every MP3 route answers for a name no MP3 can be played by, a file name no MP3 can be
// stored under, and a name nothing is stored under.
HttpResult invalidMp3Name();
HttpResult invalidMp3FileName();
HttpResult missingMp3();

// Empty input is the root; callers default an absent ?dir= to /ICONS.
// Unsafe paths are refused before I/O.
HttpResult listDirectory(const std::string& requested, std::string& normalized);

struct UploadTarget {
  std::string path;
  HttpResult result;
  bool ok() const { return result.status == 200; }
};
UploadTarget prepareUpload(const std::string& directory, const std::string& filename);

using UploadValidator = assets::UploadValidator;

HttpResult finishUpload(const std::string& path, bool contentOk, bool writeOk);

// Serializes one entry at a time.
class JsonListing {
 public:
  // Mp3 is Files plus the "scripts" groups GET /api/v1/audio/mp3 lists after the shared files.
  enum class Kind { Files, Melodies, Mp3 };
  using Emit = std::function<void(const std::string&)>;

  JsonListing(Kind kind, Emit emit);
  void begin();
  // sha256 is written only when there is one.
  void file(const std::string& name, uint64_t size, const std::string& sha256 = {});
  void melody(const std::string& filename, const std::string& content, uint32_t size);
  // Kind::Mp3: the files after this belong to one script's folder. A group is written with its
  // first file, so a script whose folder is empty is not listed. orphan marks sounds whose script
  // was deleted without them.
  void group(const std::string& script, const std::string& title, bool orphan = false);
  void end(uint64_t usedBytes, uint64_t totalBytes);

 private:
  void entry(std::string json);
  Kind kind_;
  Emit emit_;
  bool first_ = true;
  bool grouping_ = false;
  bool groupOpen_ = false;
  bool firstGroup_ = true;
  std::string script_;
  std::string title_;
  bool orphan_ = false;
};

}
