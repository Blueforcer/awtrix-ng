#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace awtrix::iconorigins {

inline constexpr const char* kPath = "/config/icon-origins.json";
inline constexpr std::size_t kMaxRecords = 64;
inline constexpr std::size_t kMaxBytes = 16 * 1024;
inline constexpr std::size_t kMaxPutBytes = 1024;

struct Record {
  std::string name, hub, slug, sha256;
};

// These are local, user-supplied links, not signatures or proof of authorship.
// sha256 describes bytes at linkage time; editing the file must not update it.
class Backend {
 public:
  virtual ~Backend() = default;
  // A missing store is a successful read of an empty string.
  virtual bool read(std::string& out) = 0;
  virtual bool writeAtomic(const std::string& json) = 0;
  virtual bool iconExists(const std::string& name) = 0;
};

struct Result {
  int status;
  std::string body;
};

bool validName(const std::string& name);
bool parseCollection(const std::string& json, std::vector<Record>& out);
std::string serialize(const std::vector<Record>& records);
Result handle(Backend& storage, const std::string& method, const std::string& body = {},
              const std::string& name = {});
// Restore after all assets, discarding references whose files do not exist.
bool restore(Backend& storage, const std::string& json, std::string& error);

}
