#pragma once

#include <functional>
#include <string>

#include "core/api/HttpExchange.h"
#include "core/api/ScriptSoundsApi.h"
#include "core/icons/IconOrigins.h"

namespace awtrix::api {

// Lists visit one entry at a time. Implementations confine every operation to device storage.
class Files : public asset::Backend, public scriptsounds::Backend {
 public:
  struct Entry { const char* name; uint64_t size; bool directory; };
  using Visit = std::function<void(const Entry&)>;

  Files(const scriptsounds::InstalledScripts& installed, iconorigins::Backend& origins)
      : installed(installed), origins(origins) { iconOrigins = &origins; }
  virtual ~Files() = default;
  virtual void list(const std::string& directory, const Visit& visit) = 0;
  virtual bool exists(const std::string& path) = 0;
  virtual bool read(const std::string& path, std::string& content) = 0;
  virtual bool write(const std::string& path, const std::string& content) = 0;
  virtual bool remove(const std::string& path) = 0;
  virtual bool rename(const std::string& from, const std::string& to) = 0;
  virtual bool mkdir(const std::string& directory, bool& created) = 0;
  virtual void prune(const std::string& directory) = 0;
  virtual void usage(uint64_t& used, uint64_t& total) = 0;
  virtual bool etag(const std::string& path, std::string& value) = 0;
  virtual bool sha256(const std::string& path, std::string& value, uint64_t& size) = 0;
  virtual void release(const std::string& path) = 0;
  virtual void changed() = 0;

  const scriptsounds::InstalledScripts& installed;
  iconorigins::Backend& origins;
  void eachFile(const std::string& directory, const scriptsounds::Backend::Visit& visit) override;
};

bool routeFiles(const Request& request, Reply& reply, Files& files);
asset::UploadTarget uploadTarget(const Request& request, const std::string& filename,
                                 const scriptsounds::InstalledScripts& installed);

}
