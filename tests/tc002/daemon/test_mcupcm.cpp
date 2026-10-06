#include "platform/tc002/daemon/McuPcm.h"
#include "support.h"

#include <cstring>

using namespace awtrix::tc002d;
using tc002d_test::check;

namespace {
mcu::Frame metadata(unsigned type, unsigned count, unsigned status = 0) {
  mcu::Frame f;
  f.command = mcu::kPcm; f.length = 16;
  std::memcpy(f.payload, "PCM2", 4);
  f.payload[4] = type; f.payload[5] = status;
  f.payload[6] = count; f.payload[7] = count >> 8;
  return f;
}
mcu::Frame part(unsigned sequence, unsigned index) {
  mcu::Frame f;
  f.command = mcu::kPcm; f.length = 16;
  f.payload[0] = 'P'; f.payload[1] = '2';
  f.payload[2] = sequence; f.payload[3] = sequence >> 8;
  f.payload[4] = index; f.payload[5] = sequence % 2;
  for (unsigned i = 0; i < 4; ++i) {
    const auto value = static_cast<uint16_t>(sequence * 24 + index * 4 + i - 32000);
    f.payload[8 + i * 2] = value; f.payload[9 + i * 2] = value >> 8;
  }
  return f;
}
void body(mcu::PcmCapture& capture, unsigned halves) {
  for (unsigned i = 0; i < halves; ++i)
    for (unsigned p = 0; p < 6; ++p) capture.receive(part(i, p));
}
}

int main() {
  tc002d_test::quietLogs();
  mcu::PcmCapture capture;
  check(!capture.start(0, 10) && !capture.start(mcu::PcmCapture::kMaxHalves + 1, 10), "bounded request size");
  check(capture.start(44, 10), "start capture");
  check(!capture.start(1, 11), "one capture in flight");
  capture.receive(metadata(1, 0)); body(capture, 44);
  check(!capture.succeeded(), "complete sample payload still requires end metadata");
  capture.receive(metadata(2, 44));
  check(capture.succeeded() && !capture.active() && capture.samples().size() == 1056, "clean snapshot");
  bool exact = true;
  for (unsigned i = 0; i < capture.samples().size(); ++i) exact &= capture.samples()[i] == static_cast<int>(i) - 32000;
  check(exact, "sample order and signed little-endian values");

  for (unsigned fault = 0; fault < 9; ++fault) {
    capture.start(2, 20);
    if (fault != 0) capture.receive(metadata(1, 0));
    if (fault == 1) capture.receive(metadata(1, 0));
    auto bad = part(0, 0);
    if (fault == 2) bad.payload[2] = 1;
    if (fault == 3) bad.payload[4] = 1;
    if (fault == 4) bad.payload[5] = 2;
    if (fault == 5) bad.payload[6] = 1;
    if (fault == 6) bad.length = 15;
    capture.receive(bad);
    if (fault == 7) capture.corrupt();
    if (fault == 8) capture.receive(part(0, 0));
    check(!capture.error().empty() && capture.samples().empty() && capture.active(), "corruption invalidates and drains");
    capture.receive(metadata(2, 2));
    check(!capture.active() && !capture.succeeded(), "corrupt capture never succeeds");
  }
  capture.start(2, 0); capture.receive(metadata(1, 0)); body(capture, 1); capture.receive(metadata(2, 1));
  check(!capture.succeeded() && capture.samples().empty(), "short capture refused even with a successful end");
  capture.start(1, 0); capture.receive(metadata(1, 0)); body(capture, 1); capture.receive(metadata(2, 1, 2));
  check(!capture.succeeded(), "MCU overrun status refused");
  capture.start(2, 0); capture.receive(metadata(1, 0)); body(capture, 1);
  auto repeated = part(1, 0); repeated.payload[5] = 0; capture.receive(repeated);
  check(!capture.error().empty(), "DMA halves must alternate");
  capture.tick(mcu::PcmCapture::kTimeoutMs);
  check(!capture.active() && !capture.succeeded() && capture.samples().empty(), "deadline bounds missing end");
  return tc002d_test::finish("tc002d PCM");
}
