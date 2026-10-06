#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace awtrix::tc002d::mcu {

// Incremental OEM transport. The caller owns the UART exclusively and keeps image alive.
// Timeouts are terminal: a missing ACK is not permission to retransmit flash data.
class Upgrade {
 public:
  void start(const std::vector<uint8_t>& image, int64_t now);
  void receive(const uint8_t* data, std::size_t size, int64_t now);
  void flush(int fd, int64_t now);
  void tick(int64_t now);
  void fail(const std::string& error);
  bool active() const { return phase_ == Phase::Handshake || phase_ == Phase::Config || phase_ == Phase::Data; }
  bool succeeded() const { return phase_ == Phase::Done; }
  bool pending() const { return sent_ < tx_.size(); }
  int64_t deadline() const;
  const std::string& error() const { return error_; }
  std::size_t progress() const { return offset_; }
  static constexpr int64_t kLimitMs = 120000;
  static uint32_t crc(const uint8_t* data, std::size_t size, bool file = false);

 private:
  enum class Phase { Idle, Handshake, Config, Data, Done, Failed };
  void queue(std::vector<uint8_t> bytes, int64_t now);
  void packet(int64_t now);
  void reply(const std::vector<uint8_t>& bytes, int64_t now);
  Phase phase_ = Phase::Idle;
  const std::vector<uint8_t>* image_ = nullptr;
  std::vector<uint8_t> tx_, rx_;
  std::size_t sent_ = 0, offset_ = 0, block_ = 0, packet_ = 0, blockSent_ = 0, blockBytes_ = 0;
  uint8_t txSeq_ = 1, rxSeq_ = 0;
  unsigned attempts_ = 0;
  int64_t expires_ = -1, totalExpires_ = -1;
  std::string error_;
};

}
