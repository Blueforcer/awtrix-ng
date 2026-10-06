// HardwareLease against a fake /proc + /sys tree with fake panel and input backends.
#include <sys/wait.h>

#include <cerrno>

#include "platform/tc002/daemon/HardwareLease.h"
#include "support.h"

using namespace awtrix::tc002d;
using tc002d_test::check;
using tc002d_test::readFile;
using tc002d_test::TempDir;
using tc002d_test::writeFile;

namespace {

struct PanelWorld {
  LatchState latch{true, "out", "1"};
  bool owned = false;
  SpiSettings spi{3, 8, 0, 4000000};
  unsigned exports = 0, drives = 0, restores = 0;
  SpiSettings restored;
};

class FakePanel : public PanelBackend {
 public:
  explicit FakePanel(PanelWorld& world) : w_(world) {}
  bool readLatch(LatchState& out) override {
    out = w_.latch;
    return true;
  }
  bool exportLatch() override {
    ++w_.exports;
    w_.latch = LatchState{true, "in", "0"};
    return true;
  }
  bool driveLatchHigh() override {
    ++w_.drives;
    w_.latch.direction = "out";
    w_.latch.value = "1";
    return true;
  }
  bool panelUnowned() override {
    if (w_.owned) errno = EBUSY;
    return !w_.owned;
  }
  bool readSpi(SpiSettings& out) override {
    out = w_.spi;
    return true;
  }
  bool writeSpi(const SpiSettings& settings) override {
    ++w_.restores;
    w_.restored = settings;
    return true;
  }

 private:
  PanelWorld& w_;
};

struct InputWorld {
  bool present = true;
  bool keyHeld = false;
  unsigned pendingEvents = 0;
  bool refuseGrab = false;
  int grabbedKeys = 0, grabbedKnob = 0;
  int keysFd = -1, knobFd = -1;
  unsigned drains = 0;
};

class FakeInput : public InputBackend {
 public:
  explicit FakeInput(InputWorld& world) : w_(world) {}
  bool discover(int& keys, int& knob) override {
    if (!w_.present) {
      errno = ENODEV;
      return false;
    }
    keys = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
    knob = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
    w_.keysFd = keys;
    w_.knobFd = knob;
    return true;
  }
  bool grab(int fd, bool enable) override {
    if (enable && w_.refuseGrab) {
      errno = EBUSY;
      return false;
    }
    (fd == w_.keysFd ? w_.grabbedKeys : w_.grabbedKnob) += enable ? 1 : -1;
    return true;
  }
  bool keysUp(int) override {
    if (w_.keyHeld) errno = EBUSY;
    return !w_.keyHeld;
  }
  bool drain(int, uint64_t& discarded) override {
    ++w_.drains;
    discarded += w_.pendingEvents;
    w_.pendingEvents = 0;
    return true;
  }

 private:
  InputWorld& w_;
};

struct Fixture {
  TempDir root;
  PanelWorld panel;
  InputWorld input;
  std::string otg;
  Fixture(const std::string& model = std::string(HardwareLease::kBoardModel) + std::string(1, '\0'),
          const std::string& role = "usb_host\n") {
    writeFile(root / "proc/device-tree/model", model);
    otg = root / "sys/bus/platform/devices/soc:usbotg/otg_role";
    writeFile(otg, role);
  }
  std::unique_ptr<HardwareLease> lease() {
    LeaseOptions options;
    options.root = root.path();
    return std::make_unique<HardwareLease>(options, std::make_unique<FakePanel>(panel),
                                           std::make_unique<FakeInput>(input));
  }
};

void finishUsbWrite(HardwareLease& lease, int64_t nowMs) {
  int status = 0;
  const pid_t writer = lease.otgWriter();
  check(writer > 0 && ::waitpid(writer, &status, 0) == writer && lease.onChildExit(writer, status, nowMs),
        "the usb_device writer child is claimed");
}

void boardChecks() {
  Fixture wrong("SOME OTHER BOARD\n");
  check(!wrong.lease()->start(0), "foreign board refused");
  Fixture missingInputs;
  missingInputs.input.present = false;
  check(!missingInputs.lease()->start(0), "missing input devices refused");
  Fixture busyPanel;
  busyPanel.panel.latch = LatchState{true, "out", "0"};
  busyPanel.panel.owned = true;
  check(!busyPanel.lease()->start(0), "low latch with another panel owner is left alone and refused");
  check(busyPanel.panel.drives == 0, "no latch write while the panel is owned");
}

void acquisition() {
  Fixture f;
  f.panel.latch = LatchState{};
  f.input.pendingEvents = 5;
  auto lease = f.lease();
  int64_t now = 1000;
  check(lease->start(now), "lease starts");
  check(lease->otgWrites() == 1 && lease->otgWriter() > 0, "usb_host starts one usb_device write in a child");
  finishUsbWrite(*lease, now);
  check(tc002d_test::readFile(f.otg) == "usb_device", "otg role switched to usb_device");
  check(f.panel.exports == 1 && f.panel.drives == 1 && f.panel.latch.idleHigh(), "cold-boot latch exported and driven high");
  check(!lease->childReady(), "inputs not handed out during the quiet period");
  check(lease->nextDeadlineMs() > now, "quiet period schedules a future deadline");

  f.input.pendingEvents = 2;
  now += 100;
  lease->onTime(now);
  check(!lease->childReady(), "events during the quiet period restart it");
  f.input.keyHeld = true;
  now += 100;
  lease->onTime(now);
  check(!lease->childReady() && f.input.grabbedKeys == 0, "a held button postpones the grab");
  f.input.keyHeld = false;
  now += 100;
  lease->onTime(now);
  check(lease->childReady() && f.input.grabbedKeys == 1 && f.input.grabbedKnob == 1, "both inputs grabbed");
  check(lease->keysFd() == f.input.keysFd && lease->knobFd() == f.input.knobFd, "grabbed descriptors exposed");

  check(lease->prepareChild(now), "ready for a child");
  f.panel.latch.value = "0";
  check(lease->prepareChild(now) && f.panel.latch.idleHigh() && f.panel.drives == 2,
        "latch left low by a crashed child is normalized before the next start");
  f.panel.latch.value = "0";
  f.panel.owned = true;
  check(!lease->prepareChild(now), "no normalization while someone holds the panel");
  f.panel.owned = false;
  f.input.keyHeld = true;
  check(!lease->prepareChild(now), "no child start while a button is held");
  f.input.keyHeld = false;

  const int64_t recheck = lease->nextDeadlineMs();
  check(recheck > now, "USB role is checked again after the write");
  lease->onTime(recheck);
  const int64_t poll = lease->nextDeadlineMs();
  check(poll > recheck, "subsequent role check is scheduled");
  writeFile(f.otg, "usb_host\n");
  lease->onTime(poll);
  const int64_t check1 = lease->nextDeadlineMs();
  check(lease->otgWrites() == 1 && check1 > poll, "a second USB write is rate limited");
  lease->onTime(check1);
  check(lease->otgWrites() == 2, "role re-selected after the kernel flipped it");
  finishUsbWrite(*lease, check1);
  check(readFile(f.otg) == "usb_device", "usb_device written again");

  lease->requestStop(check1);
  f.input.keyHeld = true;
  lease->onTime(check1);
  check(!lease->stopped(), "release waits for buttons to be up");
  f.input.keyHeld = false;
  lease->onTime(check1 + 20);
  check(lease->stopped(), "released once buttons are up");
  check(f.input.grabbedKeys == 0 && f.input.grabbedKnob == 0, "inputs ungrabbed");
  check(f.panel.restores == 1 && f.panel.restored.speedHz == 4000000 && f.panel.restored.mode == 3,
        "SPI settings restored from the start snapshot");
  check(lease->nextDeadlineMs() == -1, "no deadlines after release");
}

void unknownRole() {
  Fixture f(std::string(HardwareLease::kBoardModel) + "\n", "unkown\n");
  auto lease = f.lease();
  check(lease->start(0), "start");
  check(lease->otgWrites() == 0 && readFile(f.otg) == "unkown\n", "no write while otg_role reads unkown");
  lease->onTime(100);
  check(lease->childReady() && lease->nextDeadlineMs() > 100, "unknown role is polled after input becomes ready");
  int64_t now = lease->nextDeadlineMs();
  for (int i = 0; i < 6; ++i) {
    lease->onTime(now);
    now = lease->nextDeadlineMs();
  }
  check(lease->otgWrites() == 0 && readFile(f.otg) == "unkown\n", "still no write after six polls");
  writeFile(f.otg, "usb_null\n");
  lease->onTime(now);
  check(lease->otgWrites() == 1 && lease->nextDeadlineMs() == -1, "usb_null gets one write, no polling meanwhile");
  finishUsbWrite(*lease, now + 690);
  check(readFile(f.otg) == "usb_device" && lease->nextDeadlineMs() > now + 690,
        "the role is read again after the write ends");
  writeFile(f.otg, "otg\n");
  const auto later = lease->nextDeadlineMs();
  lease->onTime(later);
  check(lease->otgWrites() == 1 && lease->nextDeadlineMs() > later, "other values are only polled");
  writeFile(f.otg, "unkown\n");
  lease->onTime(25000);
  check(lease->otgWrites() == 1 && lease->nextDeadlineMs() > 25000, "unknown role continues to be polled");
  lease->requestStop(30000);
  lease->onTime(30000);
}

void releaseTimeout() {
  Fixture f;
  auto lease = f.lease();
  check(lease->start(0), "start");
  lease->onTime(100);
  check(lease->childReady(), "held");
  f.input.keyHeld = true;
  lease->requestStop(200);
  lease->onTime(200);
  check(!lease->stopped(), "waiting for release");
  lease->onTime(2200);
  check(lease->stopped() && f.input.grabbedKeys == 0, "release timeout bounds the wait");
}

void roleAlreadyDevice() {
  Fixture f(std::string(HardwareLease::kBoardModel) + "\n", "usb_device\n");
  auto lease = f.lease();
  check(lease->start(0), "start");
  check(lease->otgWrites() == 0 && readFile(f.otg) == "usb_device\n", "otg_role left alone when already usb_device");
  check(lease->otgRole() == "usb_device", "role reported");
  Fixture noRole;
  ::unlink(noRole.otg.c_str());
  auto other = noRole.lease();
  check(other->start(0), "missing otg_role is not fatal");
}

void grabRefused() {
  Fixture f;
  f.input.refuseGrab = true;
  auto lease = f.lease();
  check(lease->start(0), "start");
  lease->onTime(100);
  check(!lease->childReady() && lease->nextDeadlineMs() > 100, "refused grab schedules a retry");
  f.input.refuseGrab = false;
  lease->onTime(lease->nextDeadlineMs());
  check(lease->childReady(), "grab succeeds later");
}

void nativePanelFiles() {
  TempDir root;
  auto panel = nativePanelBackend(root.path());
  LatchState latch;
  check(panel->readLatch(latch) && !latch.exported, "missing gpio35 reads as not exported");
  writeFile(root / "sys/class/gpio/export", "");
  check(panel->exportLatch() && readFile(root / "sys/class/gpio/export") == "35", "export writes 35");
  writeFile(root / "sys/class/gpio/gpio35/direction", "in\n");
  writeFile(root / "sys/class/gpio/gpio35/value", "0\n");
  check(panel->readLatch(latch) && latch.exported && latch.direction == "in" && latch.value == "0", "latch read");
  check(panel->driveLatchHigh() && readFile(root / "sys/class/gpio/gpio35/direction") == "high",
        "drive high writes \"high\" to direction");
  writeFile(root / "dev/spidev0.0", "");
  check(!panel->panelUnowned(), "a non-character SPI node is never treated as unowned");
  auto input = nativeInputBackend(root.path());
  writeFile(root / "dev/input/event0", "");
  int keys = -1, knob = -1;
  check(!input->discover(keys, knob) && keys < 0 && knob < 0, "regular files are not input devices");
}

}

int main() {
  tc002d_test::quietLogs();
  boardChecks();
  acquisition();
  unknownRole();
  releaseTimeout();
  roleAlreadyDevice();
  grabRefused();
  nativePanelFiles();
  return tc002d_test::finish("tc002d lease");
}
