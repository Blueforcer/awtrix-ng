#include "hal/SensorBus.h"

#include <Adafruit_BME280.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_HTU21DF.h>
#include <Adafruit_SHT31.h>
#include <Arduino.h>
#include <Wire.h>

namespace awtrix {

namespace {
constexpr uint32_t kSensorTaskIntervalMs = 2000;
constexpr uint32_t kSensorTaskStack = 4096;
}

struct SensorBus::Impl {
  enum Type { None, BME280, BMP280, HTU21DF, SHT31 } type = None;
  int sda = 21, scl = 22;
  Adafruit_BME280 bme;
  Adafruit_BMP280 bmp;
  Adafruit_HTU21DF htu;
  Adafruit_SHT31 sht;

  SensorReading latest;
  portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

  // Auto-detect by probing the usual addresses: 0x76/0x77 for BME/BMP280, HTU21DF on its fixed
  // one, SHT31 on 0x44. First chip that answers wins, so the order decides ambiguous cases.
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
    }
    if (type == None) return;
    latest = readBlocking();
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

  bool hasHumidity() const { return type == BME280 || type == HTU21DF || type == SHT31; }
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
        r.temperatureC = sht.readTemperature(); r.humidity = sht.readHumidity();
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
