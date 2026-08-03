#pragma once

#include <Arduino.h>
#include <HardwareSerial.h>

#include <cstdlib>

#include "hal/ISoundBackend.h"

namespace awtrix {

// Drives a DFPlayer Mini over a 9600 baud UART. Every exchange is a 10-byte frame and the module
// answers asynchronously, so commands are fired from tick() and never block the render loop.
class DFPlayerBackend : public ISoundBackend {
 public:
  void setPins(int rx, int tx) { rx_pin_ = rx; tx_pin_ = tx; }
  void begin() override {
    if (rx_pin_ < 0 || tx_pin_ < 0) return;
    serial_.begin(9600, SERIAL_8N1, rx_pin_, tx_pin_);
    delay(200);
    // 0x3F asks which storage devices are online; the delays cover the module's own boot, which
    // is the only place this driver is allowed to stall.
    sendCmd(0x3F, 0x0000, 0x00);
    delay(200);
    setVolume(volume_);
  }
  void setVolume(uint8_t volume) override {
    volume_ = volume > 30 ? 30 : volume;
    sendCmd(0x06, volume_, 0x00);
  }
  // The module drops a play command that arrives while a track is still running, so this stops
  // first (0x16) and holds the track back until the stop is acknowledged or the timeout expires.
  bool playFile(const std::string& id) override {
    const int track = std::atoi(id.c_str());
    if (track <= 0) return false;
    retries_ = kMaxRetries;
    sendCmd(0x16, 0);
    pendingTrack_ = track;
    playAtMs_ = millis() + kStopReplyTimeoutMs;
    playing_ = true;
    return true;
  }
  void playRtttl(const std::string& ) override {}
  bool supportsRtttl() const override { return false; }
  void stop() override {
    pendingTrack_ = 0;
    sendCmd(0x16, 0, 0x00);
    playing_ = false;
  }
  void tick() override {
    while (serial_.available()) {
      const int b = serial_.read();
      if (b < 0) break;
      // Resynchronise on the 0x7E start byte and only trust a frame that ends with 0xEF, since
      // noise on the line is common on these boards.
      if (rxLen_ == 0 && b != 0x7E) continue;
      rx_[rxLen_++] = static_cast<uint8_t>(b);
      if (rxLen_ == sizeof(rx_)) {
        if (rx_[9] == 0xEF) onFrame();
        rxLen_ = 0;
      }
    }
    if (pendingTrack_ > 0 && static_cast<int32_t>(millis() - playAtMs_) >= 0) sendPending();
  }
  bool isPlaying() const override { return playing_; }

 private:
  // Byte 3 is the reply code: 0x3D track finished, 0x41 command acknowledged, 0x40 error — which
  // is worth retrying a couple of times, as these modules drop the odd frame.
  void onFrame() {
    switch (rx_[3]) {
      case 0x3D:
        playing_ = false;
        break;
      case 0x41:
        if (pendingTrack_ > 0) playAtMs_ = millis() + kPostStopMs;
        break;
      case 0x40:
        if (retries_ > 0 && lastAckCmd_ != 0) {
          --retries_;
          sendCmd(lastAckCmd_, lastAckParam_);
          if (pendingTrack_ > 0) playAtMs_ = millis() + kStopReplyTimeoutMs;
        }
        break;
      default:
        break;
    }
  }
  void sendPending() {
    const int track = pendingTrack_;
    pendingTrack_ = 0;
    // 0x12 plays track N from the /MP3 folder, so the sound id is just the file number.
    sendCmd(0x12, static_cast<uint16_t>(track));
    playing_ = true;
  }

  // Frame layout: 7E FF 06 cmd ack paramHi paramLo csumHi csumLo EF, where the checksum is the
  // two's complement of bytes 1..6. ack 0x01 asks the module to confirm, which enables retries.
  void sendCmd(uint8_t cmd, uint16_t param, uint8_t ack = 0x01) {
    uint8_t buf[10] = {0x7E, 0xFF, 0x06, cmd, ack,
                       static_cast<uint8_t>(param >> 8), static_cast<uint8_t>(param & 0xFF),
                       0x00, 0x00, 0xEF};
    const uint16_t cs = static_cast<uint16_t>(
        -(buf[1] + buf[2] + buf[3] + buf[4] + buf[5] + buf[6]));
    buf[7] = static_cast<uint8_t>(cs >> 8);
    buf[8] = static_cast<uint8_t>(cs & 0xFF);
    serial_.write(buf, sizeof(buf));
    if (ack != 0x00) {
      lastAckCmd_ = cmd;
      lastAckParam_ = param;
    }
  }

  static constexpr uint32_t kStopReplyTimeoutMs = 150;
  static constexpr uint32_t kPostStopMs = 50;
  static constexpr uint8_t kMaxRetries = 2;

  HardwareSerial& serial_ = Serial1;
  int rx_pin_ = 23, tx_pin_ = 18;
  uint8_t volume_ = 25;
  bool playing_ = false;
  int pendingTrack_ = 0;
  uint32_t playAtMs_ = 0;
  uint8_t lastAckCmd_ = 0;
  uint16_t lastAckParam_ = 0;
  uint8_t retries_ = 0;
  uint8_t rx_[10] = {0};
  uint8_t rxLen_ = 0;
};

}
