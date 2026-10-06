#pragma once

#include <functional>

#include "platform/linux/ble/BleTypes.h"

namespace awtrix::ble {

// Pairing records in the kernel management format, persisted as hexadecimal JSON.
class BondStore {
 public:
  static constexpr std::size_t kLtkInfo = 36;
  static constexpr std::size_t kIrkInfo = 23;
  BondStore(const std::string& path, const std::function<void(const std::string&)>& log)
      : path_(path), log_(log) {}
  void load();
  void rememberLtk(const Bytes& key);
  void rememberIrk(const Bytes& key);
  std::vector<Address> peers() const;
  bool contains(const Address& peer) const;
  void forget(const Address& peer);
  const std::vector<Bytes>& ltks() const { return ltks_; }
  const std::vector<Bytes>& irks() const { return irks_; }

 private:
  void save() const;
  const std::string& path_;
  const std::function<void(const std::string&)>& log_;
  std::vector<Bytes> ltks_, irks_;
};

}  // namespace awtrix::ble
