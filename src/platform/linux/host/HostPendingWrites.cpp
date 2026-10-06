#include "platform/linux/host/HostPersistence.h"

#include <array>
#include <mutex>

#include "platform/linux/host/HostStore.h"
#include "system/Log.h"

namespace awtrix::host::persistence {
namespace {
struct Write {
  bool dirty = false;
  std::string root;
  std::string json;
};
constexpr std::array<const char*, 4> paths{
    "/settings.json", "/device.json", "/apploop.json", "/radio.json"};
constexpr std::array<const char*, 4> labels{"settings", "device", "app order", "radio"};
std::array<Write, 4> writes;
std::mutex mutex;

bool flush(std::size_t index) {
  auto& write = writes[index];
  if (!write.dirty) return true;
  // A retry must never transplant a previous instance's config to a new root.
  if (write.root != host::dataDir() || !host::writeFile(host::hostPath(paths[index]), write.json))
    return false;
  write = Write{};
  return true;
}
}

bool save(Document document, const std::string& json) {
  const auto index = static_cast<std::size_t>(document);
  if (index >= writes.size()) return false;
  std::lock_guard<std::mutex> lock(mutex);
  auto& write = writes[index];
  const bool alreadyPending = write.dirty;
  if (alreadyPending && write.root != host::dataDir()) return false;
  write.dirty = true;
  write.root = host::dataDir();
  write.json = json;
  const bool saved = flush(index);
  if (!saved && !alreadyPending)
    logf("host persistence: %s remains pending after a write failure", labels[index]);
  return saved;
}

bool pending(Document document) {
  const auto index = static_cast<std::size_t>(document);
  std::lock_guard<std::mutex> lock(mutex);
  return index < writes.size() && writes[index].dirty;
}

bool hasPending() {
  std::lock_guard<std::mutex> lock(mutex);
  for (const auto& write : writes) if (write.dirty) return true;
  return false;
}

bool flushPending() {
  std::lock_guard<std::mutex> lock(mutex);
  bool saved = true;
  for (std::size_t index = 0; index < writes.size(); ++index)
    if (!flush(index)) saved = false;
  return saved;
}
}
