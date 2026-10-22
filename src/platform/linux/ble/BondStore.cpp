#include "platform/linux/ble/BondStore.h"

#include <algorithm>
#include <cerrno>
#include <sys/random.h>

#include "core/api/JsonReader.h"
#include "core/api/JsonWriter.h"
#include "platform/linux/ble/LinuxBleProtocol.h"

namespace awtrix::ble {
using namespace linux_detail;

void BondStore::rememberLtk(const Bytes& key) {
  ltks_.erase(std::remove_if(ltks_.begin(), ltks_.end(), [&](const Bytes& stored) {
    return std::equal(stored.begin(), stored.begin() + 7, key.begin()) && stored[8] == key[8];
  }), ltks_.end());
  ltks_.push_back(key);
  save();
}

void BondStore::rememberIrk(const Bytes& key) {
  irks_.erase(std::remove_if(irks_.begin(), irks_.end(), [&](const Bytes& stored) {
    return std::equal(stored.begin(), stored.begin() + 7, key.begin());
  }), irks_.end());
  irks_.push_back(key);
  save();
}

bool BondStore::ensureLocalIrk() {
  if (!localIrk_.empty()) return true;
  Bytes key(16);
  std::size_t filled = 0;
  while (filled < key.size()) {
    const ssize_t n = getrandom(key.data() + filled, key.size() - filled, 0);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) return false;
    filled += static_cast<std::size_t>(n);
  }
  localIrk_ = std::move(key);
  // A new identity must reach disk before it is shared with a peer.
  if (save()) return true;
  localIrk_.clear();
  return false;
}

bool BondStore::contains(const Address& peer) const {
  const auto match = [&](const Bytes& key) { return std::equal(peer.b.begin(), peer.b.end(), key.begin()); };
  return std::any_of(ltks_.begin(), ltks_.end(), match) || std::any_of(irks_.begin(), irks_.end(), match);
}

void BondStore::forget(const Address& peer) {
  const auto match = [&](const Bytes& key) { return std::equal(peer.b.begin(), peer.b.end(), key.begin()); };
  ltks_.erase(std::remove_if(ltks_.begin(), ltks_.end(), match), ltks_.end());
  irks_.erase(std::remove_if(irks_.begin(), irks_.end(), match), irks_.end());
  save();
}

std::vector<Address> BondStore::peers() const {
  std::vector<Address> out;
  for (const Bytes& k : ltks_) {
    const Address a = fromWire(k.data(), k[6]);
    if (std::find(out.begin(), out.end(), a) == out.end()) out.push_back(a);
  }
  return out;
}

void BondStore::load() {
  if (path_.empty()) return;
  std::string text;
  if (!posix::readText(path_, text, 64 * 1024)) return;
  std::string local;
  Bytes key;
  if (api::memberValue(api::JsonReader(text), "local_irk").appendString(local) &&
      fromHex(local, key) && key.size() == 16 &&
      std::any_of(key.begin(), key.end(), [](uint8_t b) { return b != 0; })) localIrk_ = std::move(key);
  auto read = [&](const char* name, std::size_t size, std::vector<Bytes>& out) {
    api::JsonReader list = api::memberValue(api::JsonReader(text), name);
    if (!list.enterArray()) return;
    while (list.nextElement()) {
      std::string hex;
      Bytes key;
      if (list.appendString(hex) && fromHex(hex, key) && key.size() == size) out.push_back(std::move(key));
      if (!list.skipValue()) break;
    }
  };
  read("ltk", kLtkInfo, ltks_);
  read("irk", kIrkInfo, irks_);
}

bool BondStore::save() const {
  if (path_.empty()) return true;
  std::string out;
  api::JsonWriter w(out);
  w.beginObject().key("ltk").beginArray();
  for (const Bytes& k : ltks_) w.value(toHex(k));
  w.endArray().key("irk").beginArray();
  for (const Bytes& k : irks_) w.value(toHex(k));
  w.endArray();
  if (!localIrk_.empty()) w.key("local_irk").value(toHex(localIrk_));
  w.endObject();
  const bool saved = posix::replaceText(path_, out);
  if (!saved && log_) log_("ble: cannot store pairing keys in " + path_);
  return saved;
}

}  // namespace awtrix::ble
