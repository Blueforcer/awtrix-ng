#include "hal/SensorBus.h"

#include <Adafruit_BME280.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_HTU21DF.h>
#include <Adafruit_SHT31.h>
#include <Arduino.h>
#include <Wire.h>

#include <cstring>

namespace awtrix {

namespace {
constexpr uint32_t kSensorTaskIntervalMs = 2000;
constexpr uint32_t kSensorTaskStack = 4096;

constexpr uint8_t kAhtAddress = 0x38;
constexpr uint8_t kAhtBusy = 0x80;
constexpr uint8_t kAhtCalibrated = 0x08;
constexpr uint32_t kAhtWaitMs = 100;

bool ahtSend(uint8_t command, uint8_t arg0, uint8_t arg1, uint8_t length) {
  const uint8_t bytes[] = {command, arg0, arg1};
  Wire.beginTransmission(kAhtAddress);
  Wire.write(bytes, length);
  return Wire.endTransmission() == 0;
}

bool ahtReadIdle(uint8_t* out, uint8_t length) {
  const uint32_t start = millis();
  for (;;) {
    if (Wire.requestFrom(kAhtAddress, length) != length) return false;
    for (uint8_t i = 0; i < length; ++i) out[i] = static_cast<uint8_t>(Wire.read());
    if (out[0] == 0xFF) return false;
    if (!(out[0] & kAhtBusy)) return true;
    if (millis() - start >= kAhtWaitMs) return false;
    delay(10);
  }
}

bool ahtInit() {
  uint8_t status;
  if (!ahtReadIdle(&status, 1) || !ahtSend(0xBA, 0, 0, 1)) return false;
  delay(20);
  if (!ahtReadIdle(&status, 1)) return false;
  for (const uint8_t command : {0xE1, 0xBE}) {
    ahtSend(command, 0x08, 0x00, 3);
    if (!ahtReadIdle(&status, 1)) return false;
    if (status & kAhtCalibrated) return true;
  }
  return false;
}

void ahtMeasure(SensorReading& r) {
  if (!ahtSend(0xAC, 0x33, 0x00, 3)) return;
  delay(80);
  uint8_t d[6];
  if (!ahtReadIdle(d, sizeof d) || !(d[0] & kAhtCalibrated)) return;
  if (!memcmp(d, d + 1, sizeof d - 1)) return;
  const uint32_t h = (uint32_t{d[1]} << 12) | (uint32_t{d[2]} << 4) | (d[3] >> 4);
  const uint32_t t = (uint32_t{d[3] & 0x0Fu} << 16) | (uint32_t{d[4]} << 8) | d[5];
  r.present = true; r.hasHumidity = true;
  r.humidity = static_cast<float>(h) * 100.0f / 0x100000;
  r.temperatureC = static_cast<float>(t) * 200.0f / 0x100000 - 50.0f;
}
}

struct SensorBus::Impl {
  enum Type { None, BME280, BMP280, HTU21DF, SHT31, AHT10 } type = None;
  int sda = 21, scl = 22;
  Adafruit_BME280 bme;
  Adafruit_BMP280 bmp;
  Adafruit_HTU21DF htu;
  Adafruit_SHT31 sht;

  SensorReading latest;
  portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

  // Auto-detect by probing the usual addresses: 0x76/0x77 for BME/BMP280, HTU21DF on its fixed
  // one, SHT31 on 0x44, AHT10/AHT20 on 0x38. First chip that answers wins, so the order decides
  // ambiguous cases.
  void begin() {
    if (sda < 0 || scl < 0) return;
    Wire.begin(sda, scl);
    if (bme.begin(0x76) || bme.begin(0x77)) {
      type = BME280;
    } else if (bmp.begin(0x76) || bmp.begin(0x77)) {
      type = BMP280;
    } else if (htu.begin()) {
      type = HTU21DF;
    } else if (sht.begin(0x44)) {
      type = SHT31;
    } else if (ahtInit()) {
      type = AHT10;
    }
    if (type == None) return;
    latest = readBlocking();
    if (type == AHT10 && !latest.present) {
      type = None;
      return;
    }
    // An I2C conversion takes tens of milliseconds, far too long for the render loop, so a task on
    // core 0 samples in the background and read() only ever hands out the last snapshot.
    xTaskCreatePinnedToCore(
        [](void* self) {
          Impl& impl = *static_cast<Impl*>(self);
          for (;;) {
            const SensorReading r = impl.readBlocking();
            portENTER_CRITICAL(&impl.mux);
            impl.latest = r;
            portEXIT_CRITICAL(&impl.mux);
            vTaskDelay(pdMS_TO_TICKS(kSensorTaskIntervalMs));
          }
        },
        "sensors", kSensorTaskStack, this, 1, nullptr, 0);
  }

  bool hasHumidity() const { return type == BME280 || type == HTU21DF || type == SHT31 || type == AHT10; }
  bool hasPressure() const { return type == BME280 || type == BMP280; }

  SensorReading read() {
    portENTER_CRITICAL(&mux);
    const SensorReading r = latest;
    portEXIT_CRITICAL(&mux);
    return r;
  }

  SensorReading readBlocking() {
    SensorReading r;
    switch (type) {
      case BME280:
        r.present = true; r.hasHumidity = true; r.hasPressure = true;
        r.temperatureC = bme.readTemperature(); r.humidity = bme.readHumidity();
        r.pressureHpa = bme.readPressure() / 100.0f;
        break;
      case BMP280:
        r.present = true; r.hasPressure = true;
        r.temperatureC = bmp.readTemperature(); r.pressureHpa = bmp.readPressure() / 100.0f;
        break;
      case HTU21DF:
        r.present = true; r.hasHumidity = true;
        r.temperatureC = htu.readTemperature(); r.humidity = htu.readHumidity();
        break;
      case SHT31:
        r.present = true; r.hasHumidity = true;
        sht.readBoth(&r.temperatureC, &r.humidity);
        break;
      case AHT10:
        ahtMeasure(r);
        break;
      default:
        break;
    }
    return r;
  }

  const char* name() const {
    switch (type) {
      case BME280: return "BME280";
      case BMP280: return "BMP280";
      case HTU21DF: return "HTU21DF";
      case SHT31: return "SHT31";
      case AHT10: return "AHT10";
      default: return "none";
    }
  }
};

SensorBus::SensorBus() : impl_(new Impl()) {}
SensorBus::~SensorBus() { delete impl_; }
void SensorBus::setPins(int sda, int scl) { impl_->sda = sda; impl_->scl = scl; }
void SensorBus::begin() { impl_->begin(); }
bool SensorBus::hasSensor() const { return impl_->type != Impl::None; }
bool SensorBus::hasHumidity() const { return impl_->hasHumidity(); }
bool SensorBus::hasPressure() const { return impl_->hasPressure(); }
SensorReading SensorBus::read() { return impl_->read(); }
const char* SensorBus::sensorName() const { return impl_->name(); }

}
