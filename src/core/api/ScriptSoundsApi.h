#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "core/api/ApiRouter.h"
#include "core/api/AssetApi.h"
#include "core/script/ScriptConfig.h"

namespace awtrix::script {
class ScriptHost;
}

// A script's own sounds: /SCRIPTS/<script>/<sound>.mp3, next to its source. The routes live under
// the script they belong to. Files adapters provide the storage and streaming hash operations.
namespace awtrix::api::scriptsounds {

// /api/v1/apps/script/{script}/sounds and .../sounds/{sound}, matched by shape alone.
// Malformed names are refused by the matching route.
struct Route {
  bool matched = false;
  bool item = false;
  std::string script;
  std::string sound;
};

Route match(std::string_view path);

// The collection accepts uploads. Allocates nothing.
bool isUpload(std::string_view path);

// The scripts these routes can reach, seen the way GET /api/v1/apps sees them: the running
// interpreter's, or the stored sources while scripting is off. Borrowed for one request.
class InstalledScripts {
 public:
  InstalledScripts(const script::ScriptHost* host, const script::ConfigTextFn& source)
      : host_(host), source_(source) {}

  // title receives the script's @name, or its install name when it declares none.
  bool find(const std::string& name, std::string* title = nullptr) const;

 private:
  const script::ScriptHost* host_;
  const script::ConfigTextFn& source_;
};

// 200 for a script that is installed or still has sounds of its own (deleting a script keeps
// them), else the invalidName or notFound answer.
HttpResult checkSounds(const std::string& script, const InstalledScripts& scripts, bool hasSounds);

// Where an upload to a script's folder lands. Both names are checked before anything is looked up.
asset::UploadTarget uploadTarget(const std::string& script, const std::string& filename,
                                 const InstalledScripts& scripts);

class Backend {
 public:
  using Visit = std::function<void(const std::string& name)>;
  virtual ~Backend() = default;
  virtual void release(const std::string& path) = 0;
  virtual bool remove(const std::string& path) = 0;
  virtual void eachFile(const std::string& directory, const Visit& visit) = 0;
  virtual bool sha256(const std::string& path, std::string& digest, uint64_t& size) = 0;
  virtual void prune(const std::string& directory) = 0;
  virtual void changed() = 0;
};

// Emits playable MP3 files and the hash/size of their current bytes.
void list(const std::string& script, asset::JsonListing& listing, Backend& storage);

// sound is the name without ".mp3", as DELETE /api/v1/audio/mp3/{name} takes it. The script need
// not be installed: sounds kept from a deleted script are deleted the same way.
HttpResult deleteSound(const std::string& script, const std::string& sound, Backend& storage);

// Every sound of a script, and its folder. 200 also when there was nothing to delete.
HttpResult deleteSounds(const std::string& script, Backend& storage);

}
