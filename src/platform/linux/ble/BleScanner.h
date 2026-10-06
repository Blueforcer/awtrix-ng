#pragma once

#include <functional>
#include <map>
#include <optional>
#include <set>

#include "platform/linux/ble/BleRadio.h"
#include "platform/linux/ble/ResourceKey.h"

namespace awtrix::ble {

// Script scan filters sharing one controller scan and bounded advertisement history.
class BleScanner {
 public:
  using Sink = std::function<void(BleEvent)>;
  struct Limits { std::size_t perScript; int eventsPerSecond; };
  BleScanner(BleRadio& radio, Sink& sink, const Power& power, const int64_t& now, Limits limits)
      : radio_(radio), sink_(sink), power_(power), now_(now), limits_(limits) {}
  std::string start(const ResourceKey& key, const std::string& args);
  bool erase(const ResourceKey& key);
  bool empty() const { return scans_.empty(); }
  void collect(const std::string& script, std::set<ResourceKey>& keys) const;
  void forgetBudget(const std::string& script) { buckets_.erase(script); }
  void apply();
  void resetRadio() { scanning_ = false; }
  void onAdvert(const AdvReport& report);
  std::optional<Address> addressFilter(const ResourceKey& key) const;
  void reportLinked(const ResourceKey& key, const Address& peer);
  void reportLinked(const Address& peer);

 private:
  struct Scan {
    bool active = false;
    std::vector<Uuid> uuids;
    bool byAddress = false;
    Address address;
    bool background = false;
    std::string name;
    int manufacturer = -1;
    int64_t dedupeMs = 1000;
    std::map<std::array<uint8_t, 6>, int64_t> lastSent;
  };
  struct Seen {
    Bytes adv;
    Bytes scanResponse;
    bool connectable = false;
    int64_t at = 0;
  };
  struct Bucket { double tokens = 0; int64_t at = 0; };
  bool allowAdvertEvent(const std::string& script);
  void emit(const ResourceKey& key, const std::string& json);
  BleRadio& radio_;
  Sink& sink_;
  const Power& power_;
  const int64_t& now_;
  Limits limits_;
  std::map<ResourceKey, Scan> scans_;
  std::map<Address, Seen> seen_;
  std::map<std::string, Bucket> buckets_;
  bool scanning_ = false, scanActive_ = false, scanBackground_ = false;
};

}  // namespace awtrix::ble
