#include "transport/net/ArtnetService.h"

#include <WiFi.h>

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace awtrix {

namespace {
constexpr int kArtnetPort = 6454;
// A DMX universe carries 512 channels, so three bytes per pixel leaves 170 whole pixels.
constexpr int kPixelsPerUniverse = 170;
// How long the matrix stays under Art-Net control after the last packet. Senders stop without
// saying so, and this is what hands the display back to the normal apps.
constexpr long kHoldMs = 5000;
constexpr uint16_t kOpDmx = 0x5000;
constexpr uint16_t kOpPoll = 0x2000;
constexpr uint16_t kOpPollReply = 0x2100;
}

void ArtnetService::begin() {
  if (!active_) active_ = udp_.open(kArtnetPort);
}

void ArtnetService::end() {
  udp_.close();
  active_ = false;
  lastMs_ = -100000;
  frame_ = {};
}

// ArtPollReply is a fixed 239-byte record with everything at a hard-coded offset, so it is built by
// poking fields into a zeroed buffer. Offsets follow the Art-Net 4 spec.
void ArtnetService::sendPollReply() {
  uint8_t r[239];
  std::memset(r, 0, sizeof(r));
  std::memcpy(r, "Art-Net\0", 8);
  r[8] = kOpPollReply & 0xFF;
  r[9] = (kOpPollReply >> 8) & 0xFF;
  const IPAddress ip = WiFi.localIP();
  r[10] = ip[0]; r[11] = ip[1]; r[12] = ip[2]; r[13] = ip[3];
  r[14] = kArtnetPort & 0xFF;
  r[15] = (kArtnetPort >> 8) & 0xFF;
  r[16] = 1;
  r[23] = 0xD0;
  std::strncpy(reinterpret_cast<char*>(r + 26), "AWTRIX NG", 17);
  std::strncpy(reinterpret_cast<char*>(r + 44), "AWTRIX NG LED matrix", 63);
  r[172] = 0; r[173] = 1;
  r[174] = 0x80;
  r[182] = 0x80;
  uint8_t mac[6];
  WiFi.macAddress(mac);
  std::memcpy(r + 201, mac, 6);
  udp_.replyTo(kArtnetPort, r, sizeof(r));
}

// Drains every packet waiting on the socket and paints the assembled frame. Returns true while
// Art-Net owns the display, which tells the render pipeline to stay out of the way.
bool ArtnetService::tick(Canvas& out, int64_t nowMs) {
  const int total = out.width() * out.height();

  if (active_) {
    bool sessionActive = (nowMs - lastMs_) < kHoldMs;
    uint8_t buf[600];
    int n;
    while ((n = udp_.receive(buf, sizeof(buf))) > 0) {
      if (n < 12) continue;
      if (std::memcmp(buf, "Art-Net\0", 8) != 0) continue;
      const uint16_t op = buf[8] | (buf[9] << 8);
      if (op == kOpPoll) { sendPollReply(); continue; }
      if (op != kOpDmx || n < 18) continue;
      // One frame arrives as several universes, so the buffer persists between packets and is only
      // wiped when a new session starts or the matrix size changed under us.
      if (!sessionActive || static_cast<int>(frame_.size()) != total) {
        frame_.assign(total, 0u);
        sessionActive = true;
      }
      // Art-Net is inconsistent on purpose: the universe is little endian, the length big endian.
      const uint16_t universe = buf[14] | (buf[15] << 8);
      uint16_t len = (buf[16] << 8) | buf[17];
      if (len > static_cast<uint16_t>(n - 18)) len = static_cast<uint16_t>(n - 18);
      const int base = universe * kPixelsPerUniverse;
      for (int i = 0; i * 3 + 2 < len; ++i) {
        const int p = base + i;
        if (p >= total) break;
        frame_[p] = (static_cast<uint32_t>(buf[18 + i * 3]) << 16) |
                    (static_cast<uint32_t>(buf[18 + i * 3 + 1]) << 8) | buf[18 + i * 3 + 2];
      }
      lastMs_ = nowMs;
    }
  }

  const bool activeNow = (nowMs - lastMs_) < kHoldMs;
  if (activeNow) {
    if (static_cast<int>(frame_.size()) == total)
      for (int p = 0; p < total; ++p) out.setPixel(p % out.width(), p / out.width(), frame_[p]);
  } else if (!frame_.empty()) {
    frame_ = {};
  }
  return activeNow;
}

}
