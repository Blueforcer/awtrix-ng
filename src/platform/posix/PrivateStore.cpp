#include "platform/posix/PrivateStore.h"

#include <filesystem>
#include <system_error>
#include <utility>
#include <vector>

#include "platform/posix/Digest.h"
#include "platform/posix/Files.h"

namespace awtrix::posix {

PrivateStore::PrivateStore(std::string directory, std::size_t maxBytes)
    : dir_(std::move(directory)), maxBytes_(maxBytes) {}

std::string PrivateStore::path(const std::string& name) const {
  return dir_ + "/" + sha256Hex(name).substr(0, 32) + ".json";
}

bool PrivateStore::load(const std::string& name, std::string& text) const {
  return readText(path(name), text, maxBytes_);
}

bool PrivateStore::save(const std::string& name, std::string_view text) {
  if (text.size() > maxBytes_) return false;
  std::error_code error;
  std::filesystem::create_directories(std::filesystem::path(dir_).parent_path(), error);
  return ensurePrivateDirectory(dir_) && replaceText(path(name), text);
}

bool PrivateStore::erase(const std::string& name) {
  std::error_code error;
  if (!std::filesystem::remove(path(name), error)) return !error;
  return fsyncDirectory(dir_);
}

bool PrivateStore::eraseAll() {
  std::error_code error;
  std::filesystem::remove_all(dir_, error);
  return !error;
}

void PrivateStore::keepOnly(const std::function<bool(const std::string& text)>& keep) {
  std::error_code error;
  std::vector<std::filesystem::path> gone;
  std::filesystem::directory_iterator it(dir_, error), end;
  for (; !error && it != end; it.increment(error)) {
    std::string text;
    if (!readText(it->path().string(), text, maxBytes_) || !keep(text)) gone.push_back(it->path());
  }
  for (const auto& p : gone) std::filesystem::remove(p, error);
  if (!gone.empty()) fsyncDirectory(dir_);
}

}
