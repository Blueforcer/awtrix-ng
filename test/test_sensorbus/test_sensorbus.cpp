#include <unity.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

#include <Arduino.h>
#include <Wire.h>

#include "hal/SensorBus.cpp"

using namespace awtrix;

namespace {

using Clock = std::chrono::steady_clock;

class Aht : public fake::I2cDevice {
 public:
  enum class Mode { Ok, Unplugged, StuckBusy };

  explicit Aht(bool aht20) : aht20_(aht20) {}

  std::atomic<Mode> mode{Mode::Ok};
  std::atomic<int> transactions{0};

  bool write(const std::vector<uint8_t>& bytes) override {
    ++transactions;
    if (mode == Mode::Unplugged) return false;
    if (bytes == std::vector<uint8_t>{0xBA}) {
      calibrated_ = false;
      return true;
    }
    if (bytes == std::vector<uint8_t>{0xE1, 0x08, 0x00} && !aht20_) return calibrated_ = true;
    if (bytes == std::vector<uint8_t>{0xBE, 0x08, 0x00} && aht20_) return calibrated_ = true;
    if (bytes == std::vector<uint8_t>{0xAC, 0x33, 0x00}) {
      busyReads_ = 2;
      return true;
    }
    return false;
  }

  bool read(uint8_t* out, size_t length) override {
    ++transactions;
    if (mode == Mode::Unplugged) return false;
    uint8_t status = calibrated_ ? 0x18 : 0x10;
    if (mode == Mode::StuckBusy || busyReads_ > 0) status |= 0x80;
    if (busyReads_ > 0) --busyReads_;
    const uint8_t frame[6] = {status, 0x80, 0x00, 0x06, 0x00, 0x00};
    std::memcpy(out, frame, std::min<size_t>(length, sizeof frame));
    return true;
  }

 private:
  bool aht20_;
  bool calibrated_ = false;
  int busyReads_ = 0;
};

class PortExpander : public fake::I2cDevice {
 public:
  explicit PortExpander(uint8_t pinsHeldLow) : external_(static_cast<uint8_t>(~pinsHeldLow)) {}

  int writes = 0;

  bool write(const std::vector<uint8_t>& bytes) override {
    ++writes;
    if (!bytes.empty()) port_ = bytes.back();
    return true;
  }

  bool read(uint8_t* out, size_t length) override {
    std::fill(out, out + length, static_cast<uint8_t>(port_ & external_));
    return true;
  }

 private:
  uint8_t port_ = 0xFF;
  uint8_t external_;
};

class SameByte : public fake::I2cDevice {
 public:
  explicit SameByte(uint8_t value) : value_(value) {}
  bool write(const std::vector<uint8_t>&) override { return true; }
  bool read(uint8_t* out, size_t length) override {
    std::fill(out, out + length, value_);
    return true;
  }

 private:
  uint8_t value_;
};

std::unique_ptr<fake::I2cDevice> device;

template <typename T, typename... Args>
T& plugAt0x38(Args... args) {
  auto created = std::make_unique<T>(args...);
  T& ref = *created;
  std::lock_guard<std::mutex> lock(fake::i2cLock);
  device = std::move(created);
  fake::i2cDevices[0x38] = device.get();
  return ref;
}

long elapsedMs(Clock::time_point since) {
  return static_cast<long>(
      std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - since).count());
}

bool waitForPresent(SensorBus& bus, bool present) {
  const auto until = Clock::now() + std::chrono::seconds(3);
  while (bus.read().present != present) {
    if (Clock::now() > until) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return true;
}

}

void setUp() {
  fake::resetTasks();
  fake::runTasks = false;
  Adafruit_SHT31::present = false;
}

void tearDown() {
  fake::stopTasks = true;
  TEST_ASSERT_TRUE(fake::waitForNoTasks());
  std::lock_guard<std::mutex> lock(fake::i2cLock);
  fake::i2cDevices[0x38] = nullptr;
  device.reset();
}

void test_an_empty_bus_finds_nothing_at_once() {
  SensorBus bus;
  const auto start = Clock::now();
  bus.begin();
  TEST_ASSERT_FALSE(bus.hasSensor());
  TEST_ASSERT_EQUAL_STRING("none", bus.sensorName());
  TEST_ASSERT_LESS_THAN(50, elapsedMs(start));
}

void test_an_aht10_is_found_and_read() {
  plugAt0x38<Aht>(false);
  SensorBus bus;
  bus.begin();
  TEST_ASSERT_TRUE(bus.hasSensor());
  TEST_ASSERT_EQUAL_STRING("AHT10", bus.sensorName());
  TEST_ASSERT_TRUE(bus.hasHumidity());
  TEST_ASSERT_FALSE(bus.hasPressure());
  const SensorReading r = bus.read();
  TEST_ASSERT_TRUE(r.present);
  TEST_ASSERT_TRUE(r.hasHumidity);
  TEST_ASSERT_FALSE(r.hasPressure);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.0f, r.temperatureC);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 50.0f, r.humidity);
}

void test_an_aht20_that_refuses_the_aht10_init_is_found() {
  plugAt0x38<Aht>(true);
  SensorBus bus;
  bus.begin();
  TEST_ASSERT_TRUE(bus.hasSensor());
  TEST_ASSERT_TRUE(bus.read().present);
}

void test_an_earlier_sensor_wins_and_0x38_is_never_touched() {
  Aht& aht = plugAt0x38<Aht>(false);
  Adafruit_SHT31::present = true;
  SensorBus bus;
  bus.begin();
  TEST_ASSERT_EQUAL_STRING("SHT31", bus.sensorName());
  TEST_ASSERT_EQUAL_INT(0, aht.transactions.load());
}

void test_an_idle_port_expander_at_0x38_is_rejected_without_a_write() {
  PortExpander& chip = plugAt0x38<PortExpander>(0x00);
  SensorBus bus;
  const auto start = Clock::now();
  bus.begin();
  TEST_ASSERT_FALSE(bus.hasSensor());
  TEST_ASSERT_EQUAL_INT(0, chip.writes);
  TEST_ASSERT_LESS_THAN(50, elapsedMs(start));
}

void test_a_chip_that_stays_busy_is_given_up_quickly() {
  PortExpander& chip = plugAt0x38<PortExpander>(0x01);
  SensorBus bus;
  const auto start = Clock::now();
  bus.begin();
  TEST_ASSERT_FALSE(bus.hasSensor());
  TEST_ASSERT_EQUAL_INT(0, chip.writes);
  TEST_ASSERT_LESS_THAN(500, elapsedMs(start));
}

void test_a_port_expander_that_passes_the_status_check_is_still_rejected() {
  plugAt0x38<PortExpander>(0x80);
  SensorBus bus;
  const auto start = Clock::now();
  bus.begin();
  TEST_ASSERT_FALSE(bus.hasSensor());
  TEST_ASSERT_LESS_THAN(500, elapsedMs(start));
}

void test_a_chip_answering_the_same_byte_everywhere_is_not_a_sensor() {
  plugAt0x38<SameByte>(0x18);
  SensorBus bus;
  const auto start = Clock::now();
  bus.begin();
  TEST_ASSERT_FALSE(bus.hasSensor());
  TEST_ASSERT_FALSE(bus.read().present);
  TEST_ASSERT_LESS_THAN(500, elapsedMs(start));
}

void test_a_failed_read_leaves_the_reading_absent() {
  Aht& aht = plugAt0x38<Aht>(false);
  fake::runTasks = true;
  SensorBus bus;
  bus.begin();
  TEST_ASSERT_TRUE(bus.read().present);

  aht.mode = Aht::Mode::Unplugged;
  TEST_ASSERT_TRUE(waitForPresent(bus, false));
  aht.mode = Aht::Mode::Ok;
  TEST_ASSERT_TRUE(waitForPresent(bus, true));
  aht.mode = Aht::Mode::StuckBusy;
  TEST_ASSERT_TRUE(waitForPresent(bus, false));
  TEST_ASSERT_TRUE(bus.hasSensor());

  fake::stopTasks = true;
  TEST_ASSERT_TRUE(fake::waitForNoTasks());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_an_empty_bus_finds_nothing_at_once);
  RUN_TEST(test_an_aht10_is_found_and_read);
  RUN_TEST(test_an_aht20_that_refuses_the_aht10_init_is_found);
  RUN_TEST(test_an_earlier_sensor_wins_and_0x38_is_never_touched);
  RUN_TEST(test_an_idle_port_expander_at_0x38_is_rejected_without_a_write);
  RUN_TEST(test_a_chip_that_stays_busy_is_given_up_quickly);
  RUN_TEST(test_a_port_expander_that_passes_the_status_check_is_still_rejected);
  RUN_TEST(test_a_chip_answering_the_same_byte_everywhere_is_not_a_sensor);
  RUN_TEST(test_a_failed_read_leaves_the_reading_absent);
  return UNITY_END();
}
