// BtService against a pseudo terminal standing in for /dev/ttyS3: the test plays the controller
// on the master side; the line discipline and the MAC file are faked.
#include <termios.h>

#include <cstdlib>
#include <string>
#include <vector>

#include "platform/tc002/daemon/BtService.h"
#include "support.h"

using namespace awtrix::tc002d;
using tc002d_test::check;

namespace {

struct FakeSystem : BtSystem {
  std::string slave;
  std::string mac = "cc:c4:b2:77:98:c0\n";
  bool haveMac = true;
  int attaches = 0, detaches = 0;
  int open(const std::string&) override { return ::open(slave.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC); }
  bool configure(int fd) override {
    termios t{};
    if (::tcgetattr(fd, &t) != 0) return false;
    ::cfmakeraw(&t);
    return ::tcsetattr(fd, TCSANOW, &t) == 0;
  }
  bool attach(int) override {
    ++attaches;
    return true;
  }
  void detach(int) override { ++detaches; }
  bool readText(const std::string&, std::string& out) override {
    out = mac;
    return haveMac;
  }
};

struct Controller {
  int master = -1;
  std::string slave;
  Controller() {
    master = ::posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (master >= 0 && ::grantpt(master) == 0 && ::unlockpt(master) == 0) slave = ::ptsname(master);
  }
  ~Controller() {
    if (master >= 0) ::close(master);
  }
  // One whole command packet from the service, or empty.
  std::vector<uint8_t> command(int waitMs = 300) {
    std::vector<uint8_t> out;
    pollfd item{master, POLLIN, 0};
    int wait = waitMs;
    while (::poll(&item, 1, wait) > 0 && (item.revents & POLLIN)) {
      uint8_t buffer[512];
      const ssize_t n = ::read(master, buffer, sizeof buffer);
      if (n <= 0) break;
      out.insert(out.end(), buffer, buffer + n);
      if (out.size() >= 4 && out.size() >= 4u + out[3]) break;
      wait = 50;
    }
    return out;
  }
  void complete(uint16_t opcode, uint8_t status = 0) {
    const uint8_t event[] = {0x04, 0x0e, 0x04, 0x05, static_cast<uint8_t>(opcode & 0xff),
                             static_cast<uint8_t>(opcode >> 8), status};
    (void)!::write(master, event, sizeof event);
  }
};

void deliver(BtService& bt, int64_t now) {
  std::vector<PollInterest> interest;
  bt.pollInterest(interest);
  for (const auto& item : interest) {
    pollfd p{item.fd, item.events, 0};
    if (::poll(&p, 1, 300) > 0) bt.onReady(item.fd, p.revents, now);
  }
}

uint16_t opcodeOf(const std::vector<uint8_t>& packet) {
  return packet.size() >= 4 && packet[0] == 0x01 ? static_cast<uint16_t>(packet[1] | packet[2] << 8) : 0;
}

void attachAndRelease() {
  Controller controller;
  FakeSystem system;
  system.slave = controller.slave;
  std::vector<awtrix::tc002::BluetoothStatus> reports;
  BtService bt(BtOptions{}, system, [&](const awtrix::tc002::BluetoothStatus& s) { reports.push_back(s); });
  bt.start(0);
  check(bt.nextDeadlineMs() == -1 && bt.state() == BtService::State::Off, "nothing happens until the runtime asks");
  bt.request(true);
  check(bt.nextDeadlineMs() == 0, "a request is handled on the next loop turn");
  bt.onTime(0);
  const std::vector<uint16_t> expected = {0x0c03, 0xfc70, 0xfc4d, 0xfc50, 0xfc51, 0xfc52};
  std::vector<uint16_t> seen;
  std::vector<uint8_t> addressPacket, configPacket;
  for (std::size_t i = 0; i < expected.size(); ++i) {
    const std::vector<uint8_t> packet = controller.command();
    seen.push_back(opcodeOf(packet));
    if (opcodeOf(packet) == 0xfc70) addressPacket = packet;
    if (opcodeOf(packet) == 0xfc4d) configPacket = packet;
    controller.complete(opcodeOf(packet));
    deliver(bt, 10);
  }
  check(seen == expected, "the stock hciattach sequence is sent in order, one command per answer");
  check(addressPacket == std::vector<uint8_t>({0x01, 0x70, 0xfc, 0x06, 0xc0, 0x98, 0x77, 0xb2, 0xc4, 0xcc}),
        "the address is the Wi-Fi MAC, least significant byte first");
  check(configPacket.size() == 4 + 104 && configPacket[3] == 104, "the configuration block is sent whole");
  check(system.attaches == 1 && bt.state() == BtService::State::On, "the line goes to the HCI driver after the last answer");
  check(reports.size() == 1 && reports[0].on && reports[0].error.empty(), "the runtime hears the controller is on");
  bt.request(true);
  bt.onTime(20);
  check(reports.size() == 2 && reports[1].on, "asking again while on is answered, not repeated");
  bt.request(false);
  bt.onTime(30);
  check(system.detaches == 1 && bt.state() == BtService::State::Off && !reports.back().on,
        "releasing hands the line back and reports off");
}

void refusedAndSilent() {
  {
    Controller controller;
    FakeSystem system;
    system.slave = controller.slave;
    std::vector<awtrix::tc002::BluetoothStatus> reports;
    BtService bt(BtOptions{}, system, [&](const awtrix::tc002::BluetoothStatus& s) { reports.push_back(s); });
    bt.request(true);
    bt.onTime(0);
    controller.complete(opcodeOf(controller.command()));
    deliver(bt, 1);
    controller.complete(opcodeOf(controller.command()), 0x12);
    deliver(bt, 2);
    check(bt.state() == BtService::State::Off && reports.size() == 1 && !reports[0].on &&
              reports[0].error.find("fc70") != std::string::npos,
          "a refused command ends the attempt with its opcode in the error");
    check(system.attaches == 0, "a failed setup never reaches the HCI driver");
    bt.request(false);
    bt.onTime(3);
    check(reports.size() == 2 && !reports[1].on && reports[1].error.empty(),
          "letting go afterwards is answered without the old error");
  }
  {
    Controller controller;
    FakeSystem system;
    system.slave = controller.slave;
    std::vector<awtrix::tc002::BluetoothStatus> reports;
    BtOptions options;
    options.replyTimeoutMs = 100;
    BtService bt(options, system, [&](const awtrix::tc002::BluetoothStatus& s) { reports.push_back(s); });
    bt.request(true);
    bt.onTime(0);
    check(opcodeOf(controller.command()) == 0x0c03, "the reset goes out");
    check(bt.nextDeadlineMs() == options.replyTimeoutMs, "an answer is awaited for the reply timeout");
    bt.onTime(100);
    check(opcodeOf(controller.command()) == 0x0c03, "a silent controller gets the command once more");
    bt.onTime(200);
    check(bt.state() == BtService::State::Off && reports.size() == 1 &&
              reports[0].error.find("0c03") != std::string::npos,
          "after the second silence the attempt fails");
  }
  {
    FakeSystem system;
    system.haveMac = false;
    std::vector<awtrix::tc002::BluetoothStatus> reports;
    BtOptions options;
    options.driverWaitMs = 1000;
    BtService bt(options, system, [&](const awtrix::tc002::BluetoothStatus& s) { reports.push_back(s); });
    bt.request(true);
    bt.onTime(0);
    check(reports.empty() && bt.state() == BtService::State::Starting && bt.nextDeadlineMs() > 0,
          "without the Wi-Fi interface the service waits for it");
    for (int64_t t = bt.nextDeadlineMs(); t > 0 && t <= options.driverWaitMs; t = bt.nextDeadlineMs()) bt.onTime(t);
    check(reports.size() == 1 && !reports[0].error.empty() &&
              bt.state() == BtService::State::Off,
          "and gives up when the driver does not come");
  }
}

// At boot the runtime asks for Bluetooth a second before the Wi-Fi driver is loaded.
void driverLoadsLate() {
  Controller controller;
  FakeSystem system;
  system.slave = controller.slave;
  system.haveMac = false;
  std::vector<awtrix::tc002::BluetoothStatus> reports;
  BtService bt(BtOptions{}, system, [&](const awtrix::tc002::BluetoothStatus& s) { reports.push_back(s); });
  bt.request(true);
  bt.onTime(0);
  bt.onTime(250);
  check(controller.command(50).empty() && reports.empty(), "the UART stays untouched while the driver loads");
  system.haveMac = true;
  bt.onTime(500);
  check(opcodeOf(controller.command()) == 0x0c03, "the attach starts once the interface exists");
}

void garbageBeforeAnswer() {
  Controller controller;
  FakeSystem system;
  system.slave = controller.slave;
  bool on = false;
  BtService bt(BtOptions{}, system, [&](const awtrix::tc002::BluetoothStatus& s) { on = s.on; });
  bt.request(true);
  bt.onTime(0);
  for (int i = 0; i < 6; ++i) {
    const uint16_t op = opcodeOf(controller.command());
    const uint8_t noise[] = {0x00, 0xff, 0x04, 0x13, 0x01, 0x00};
    (void)!::write(controller.master, noise, sizeof noise);
    controller.complete(op);
    deliver(bt, 1);
    deliver(bt, 1);
  }
  check(on, "stray bytes and unrelated events do not derail the setup");
}

}

int main() {
  check(BtService::setup(reinterpret_cast<const uint8_t*>("\x01\x02\x03\x04\x05\x06")).size() == 6,
        "six setup commands");
  uint8_t address[6];
  check(BtService::parseMac("cc:c4:b2:77:98:c0", address) && address[0] == 0xc0 && address[5] == 0xcc,
        "a MAC is parsed into wire order");
  check(!BtService::parseMac("cc:c4:b2", address), "a short MAC is refused");
  attachAndRelease();
  refusedAndSilent();
  driverLoadsLate();
  garbageBeforeAnswer();
  return tc002d_test::finish("tc002d-bt");
}
