#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace fake {

class I2cDevice {
 public:
  virtual ~I2cDevice() = default;
  virtual bool write(const std::vector<uint8_t>& bytes) = 0;
  virtual bool read(uint8_t* out, size_t length) = 0;
};

inline std::mutex i2cLock;
inline I2cDevice* i2cDevices[128] = {};

}

class TwoWire {
 public:
  bool begin(int, int) { return true; }

  void beginTransmission(uint8_t address) {
    address_ = address;
    tx_.clear();
  }

  size_t write(const uint8_t* data, size_t length) {
    tx_.insert(tx_.end(), data, data + length);
    return length;
  }

  uint8_t endTransmission() {
    std::lock_guard<std::mutex> lock(fake::i2cLock);
    fake::I2cDevice* device = fake::i2cDevices[address_ & 0x7F];
    return device && device->write(tx_) ? 0 : 2;
  }

  uint8_t requestFrom(uint8_t address, uint8_t length) {
    std::lock_guard<std::mutex> lock(fake::i2cLock);
    fake::I2cDevice* device = fake::i2cDevices[address & 0x7F];
    rx_.assign(length, 0);
    rxIndex_ = 0;
    if (!device || !device->read(rx_.data(), length)) rx_.clear();
    return static_cast<uint8_t>(rx_.size());
  }

  int read() { return rxIndex_ < rx_.size() ? rx_[rxIndex_++] : -1; }

 private:
  uint8_t address_ = 0;
  std::vector<uint8_t> tx_;
  std::vector<uint8_t> rx_;
  size_t rxIndex_ = 0;
};

inline TwoWire Wire;
