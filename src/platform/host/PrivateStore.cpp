#include "platform/host/PrivateStore.h"

#include <cerrno>
#include <filesystem>
#include <system_error>
#include <utility>
#include <vector>

#include "platform/posix/Digest.h"
#include "platform/host/Files.h"

namespace awtrix::os {
namespace {

constexpr std::size_t kHashDigits = 32;
constexpr std::string_view kSuffix = ".json";

bool isRecordName(std::string_view leaf) {
  return leaf.size() == kHashDigits + kSuffix.size() && leaf.substr(kHashDigits) == kSuffix &&
         leaf.find_first_not_of("0123456789abcdef") == kHashDigits;
}

bool stale(const std::filesystem::path& file, std::size_t maxBytes,
           const std::function<bool(const std::string& text)>& keep) {
  if (!isRecordName(file.filename().string())) return true;
  std::string text;
  if (readFile(file.string(), text, maxBytes)) return !keep(text);
  return errno == EFBIG;
}

}

PrivateStore::PrivateStore(std::string directory, std::size_t maxBytes)
    : dir_(std::move(directory)), maxBytes_(maxBytes) {}

std::string PrivateStore::path(const std::string& name) const {
  return dir_ + "/" + posix::sha256Hex(name).substr(0, kHashDigits) + std::string(kSuffix);
}

bool PrivateStore::load(const std::string& name, std::string& text) const {
  return readFile(path(name), text, maxBytes_);
}

bool PrivateStore::save(const std::string& name, std::string_view text) {
  if (text.size() > maxBytes_) return false;
  std::error_code error;
  std::filesystem::create_directories(std::filesystem::path(dir_).parent_path(), error);
  return ensurePrivateDirectory(dir_) && replaceFile(path(name), text);
}

bool PrivateStore::erase(const std::string& name) {
  std::error_code error;
  if (!std::filesystem::remove(path(name), error)) return !error;
  return syncDirectory(dir_);
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
  for (; !error && it != end; it.increment(error))
    if (stale(it->path(), maxBytes_, keep)) gone.push_back(it->path());
  for (const auto& p : gone) std::filesystem::remove(p, error);
  if (!gone.empty()) syncDirectory(dir_);
}

}
