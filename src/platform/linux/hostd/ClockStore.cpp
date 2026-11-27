#include "platform/linux/hostd/ClockStore.h"

#include <algorithm>

#include "core/api/HttpProtocol.h"
#include "core/api/JsonReader.h"
#include "core/api/JsonWriter.h"
#include "platform/host/Files.h"
#include "platform/host/Text.h"

namespace awtrix::hostd {

namespace fs = std::filesystem;

namespace {
int number(const std::string& json, const char* name, int fallback) {
  long long value = 0;
  return api::memberValue(json, name).asLong(value) ? static_cast<int>(value) : fallback;
}
}

bool ClockStore::validUid(const std::string& uid) {
  if (uid.empty() || uid.size() > 32) return false;
  return std::all_of(uid.begin(), uid.end(), [](char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
  });
}

bool ClockStore::load(std::string& error) {
  clocks_.clear();
  std::error_code ec;
  fs::create_directories(root_ / "clocks", ec);
  if (ec) {
    error = "cannot create " + (root_ / "clocks").string() + ": " + ec.message();
    return false;
  }
  for (const auto& entry : fs::directory_iterator(root_ / "clocks", ec)) {
    std::string json, key;
    if (!entry.is_directory() || !os::readFile((entry.path() / "clock.json").string(), json) ||
        !os::readFile((entry.path() / "key").string(), key))
      continue;
    ClockRecord record;
    record.uid = api::memberText(json, "uid");
    record.name = api::memberText(json, "name");
    record.clockUrl = api::memberText(json, "clock");
    record.width = number(json, "width", 32);
    record.height = number(json, "height", 8);
    record.port = number(json, "port", 0);
    record.keyHex = os::trimmed(key);
    if (validUid(record.uid) && record.port >= kFirstPort && record.port <= kLastPort) clocks_.push_back(record);
  }
  std::sort(clocks_.begin(), clocks_.end(), [](const ClockRecord& a, const ClockRecord& b) { return a.port < b.port; });
  return true;
}

const ClockRecord* ClockStore::find(const std::string& uid) const {
  for (const ClockRecord& record : clocks_)
    if (record.uid == uid) return &record;
  return nullptr;
}

bool ClockStore::save(const ClockRecord& record, std::string& error) const {
  std::string json;
  api::JsonWriter w(json);
  w.beginObject()
      .key("uid").value(record.uid)
      .key("name").value(record.name)
      .key("clock").value(record.clockUrl)
      .key("width").value(record.width)
      .key("height").value(record.height)
      .key("port").value(record.port)
      .endObject();
  if (!os::replaceFile((clockDir(record.uid) / "clock.json").string(), json)) {
    error = "cannot write clock.json";
    return false;
  }
  return true;
}

bool ClockStore::pair(ClockRecord record, const std::string& keyHex, const std::function<bool(int)>& portFree,
                      ClockRecord& stored, std::string& error) {
  if (!validUid(record.uid)) {
    error = "invalid uid";
    return false;
  }
  const ClockRecord* known = find(record.uid);
  if (known) {
    record.port = known->port;
  } else {
    record.port = 0;
    for (int port = kFirstPort; port <= kLastPort && record.port == 0; ++port) {
      const bool taken = std::any_of(clocks_.begin(), clocks_.end(), [port](const ClockRecord& r) { return r.port == port; });
      if (!taken && portFree(port)) record.port = port;
    }
    if (record.port == 0) {
      error = "no free port";
      return false;
    }
  }
  record.keyHex = keyHex;
  std::error_code ec;
  fs::create_directories(dataDir(record.uid), ec);
  if (ec || !os::replaceFile(keyFile(record.uid).string(), keyHex + "\n") || !save(record, error)) {
    if (error.empty()) error = "cannot write the clock's folder";
    return false;
  }
  if (known) {
    *const_cast<ClockRecord*>(known) = record;
  } else {
    clocks_.push_back(record);
  }
  stored = record;
  return true;
}

bool ClockStore::remove(const std::string& uid, bool keepData) {
  const auto it = std::find_if(clocks_.begin(), clocks_.end(), [&uid](const ClockRecord& r) { return r.uid == uid; });
  if (it == clocks_.end()) return false;
  clocks_.erase(it);
  std::error_code ec;
  if (keepData) {
    fs::remove(keyFile(uid), ec);
    fs::remove(clockDir(uid) / "clock.json", ec);
  } else {
    fs::remove_all(clockDir(uid), ec);
  }
  return true;
}

bool ClockStore::keyOk(const std::string& uid, const std::string& authorization) const {
  const ClockRecord* clock = find(uid);
  return clock && api::hostKeyOk(authorization, clock->keyHex);
}

}
