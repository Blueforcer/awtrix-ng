#include <cstring>

#include "platform/tc002/daemon/McuStream.h"
#include "support.h"

using namespace awtrix::tc002d;
using tc002d_test::check;
namespace {
void put(uint8_t* p, uint32_t n) {
  for (unsigned i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(n >> (8 * i));
}
mcu::Frame meta(unsigned type, uint32_t halves, uint32_t tick) {
  mcu::Frame f;
  f.command = 6;
  f.length = 16;
  std::memcpy(f.payload, "S3", 2);
  f.payload[2] = type;
  put(f.payload + 4, 42);
  put(f.payload + 8, halves);
  put(f.payload + 12, tick);
  return f;
}
mcu::Frame part(unsigned half, unsigned index) {
  mcu::Frame f;
  f.command = 6;
  f.length = 16;
  std::memcpy(f.payload, "D3", 2);
  put(f.payload + 2, half);
  f.payload[6] = index;
  f.payload[7] = half % 2;
  for (unsigned i = 0; i < 4; ++i) {
    const uint16_t x = static_cast<uint16_t>(half * 24 + index * 4 + i - 240);
    f.payload[8 + 2 * i] = x;
    f.payload[9 + 2 * i] = x >> 8;
  }
  return f;
}
}  // namespace
int main() {
  mcu::Stream stream;
  std::vector<awtrix::tc002::StreamEvent> events;
  check(stream.start(42, 100,
                     [&](const auto& e) {
                       events.push_back(e);
                       return true;
                     }),
        "start one stream");
  auto writer = [](const uint8_t*, std::size_t) { return true; };
  stream.receive(meta(1, 0, 1000), 100, writer);
  for (unsigned h = 0; h < 20; ++h)
    for (unsigned p = 0; p < 6; ++p) stream.receive(part(h, p), 130, writer);
  check(events.size() == 1, "samples wait for a timing checkpoint");
  stream.receive(meta(3, 20, 1030), 130, writer);
  check(events.size() == 2 && events.back().samples.size() == 480,
        "one owned 30ms block");
  bool exact = events.back().firstSample == 0;
  for (unsigned i = 0; i < 480 && i < events.back().samples.size(); ++i)
    exact &= events.back().samples[i] == static_cast<int>(i) - 240;
  check(exact, "raw signed PCM is unchanged");
  stream.stop(131);
  stream.receive(meta(2, 20, 1030), 132, writer);
  check(!stream.active() && events.size() == 3 && events.back().error.empty(),
        "clean end after stop");
  events.clear();
  stream.start(42, 200, [&](const auto& e) {
    events.push_back(e);
    return true;
  });
  stream.receive(meta(1, 0, 1000), 200, writer);
  // A stop suppresses intermediate metadata while draining the ADC ring.
  for (unsigned h = 0; h < 23; ++h)
    for (unsigned p = 0; p < 6; ++p) stream.receive(part(h, p), 235, writer);
  stream.stop(235);
  stream.receive(meta(2, 23, 1034), 236, writer);
  check(events.size() == 4 && events[1].samples.size() == 480 &&
            events[2].samples.size() == 72 && events[2].firstSample == 480 &&
            events.back().error.empty(),
        "stop retains the final ring tail in bounded blocks");
  events.clear();
  stream.start(42, 300, [&](const auto& e) {
    events.push_back(e);
    return true;
  });
  stream.receive(meta(1, 0, 0xfffffff0), 300, writer);
  for (unsigned h = 0; h < 20; ++h)
    for (unsigned p = 0; p < 6; ++p) stream.receive(part(h, p), 330, writer);
  stream.receive(meta(3, 20, 14), 330, writer);
  check(events.size() == 2 && events.back().error.empty(),
        "MCU tick wrap preserves timing");
  stream.abort("cancel", 340);
  events.clear();
  stream.start(42, 400, [&](const auto& e) {
    events.push_back(e);
    return true;
  });
  stream.receive(meta(1, 0, 1000), 400, writer);
  stream.receive(part(0, 1), 401, writer);
  stream.receive(meta(2, 0, 1000), 402, writer);
  check(!stream.active() && events.size() == 2 && !events.back().error.empty(),
        "lost DMA part never publishes partial audio");
  events.clear();
  stream.start(42, 500, [&](const auto& e) {
    events.push_back(e);
    return true;
  });
  stream.receive(meta(1, 0, 1000), 500, writer);
  const auto expiry = stream.deadline();
  stream.renew(43, 900);
  check(stream.active() && stream.deadline() == expiry,
        "wrong epoch cannot renew consumer lease");
  stream.tick(expiry);
  const auto drainedAt = stream.deadline();
  check(stream.active() && drainedAt > expiry,
        "expired consumer receives a drain deadline");
  stream.tick(drainedAt);
  check(!stream.active() && events.size() == 2 && !events.back().error.empty(),
        "expired consumer drains then terminates once");
  stream.tick(drainedAt + 1);
  check(events.size() == 2, "terminal event exactly once");
  events.clear();
  stream.start(42, 4000, [&](const auto& e) {
    events.push_back(e);
    return e.kind != awtrix::tc002::StreamEvent::Kind::Audio;
  });
  stream.receive(meta(1, 0, 1000), 4000, writer);
  for (unsigned h = 0; h < 20; ++h)
    for (unsigned p = 0; p < 6; ++p) stream.receive(part(h, p), 4030, writer);
  stream.receive(meta(3, 20, 1030), 4030, writer);
  stream.receive(meta(2, 20, 1030), 4031, writer);
  check(!stream.active() && !events.back().error.empty(),
        "backpressure fails capture instead of dropping and continuing");
  return tc002d_test::finish("tc002d continuous stream");
}
