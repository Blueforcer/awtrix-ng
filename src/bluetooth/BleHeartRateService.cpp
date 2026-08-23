#include "bluetooth/BleHeartRateService.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <esp_heap_caps.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "bluetooth/HeartRateMeasurement.h"
#include "system/HeapCaps.h"
#include "system/Log.h"

namespace awtrix {
namespace bluetooth {

namespace {
constexpr uint16_t kHeartRateServiceUuid = 0x180D;
constexpr uint16_t kHeartRateMeasurementUuid = 0x2A37;
constexpr uint32_t kScanSeconds = 8;
constexpr uint32_t kRetryDelayMs = 5000;
constexpr uint32_t kWorkerStackBytes = 6144;
constexpr UBaseType_t kWorkerPriority = 1;
constexpr BaseType_t kWorkerCore = 0;
constexpr size_t kEventSlots = 10;
constexpr size_t kEventLength = 112;

}

struct BleHeartRateService::Impl : NimBLEAdvertisedDeviceCallbacks, NimBLEClientCallbacks {
  static Impl* active;

  std::atomic<bool> linkConnected{false};
  std::atomic<bool> subscribed{false};
  std::atomic<uint16_t> latestBpm{0};
  std::atomic<uint32_t> measurementSequence{0};

  std::atomic<bool> scanning{false};
  std::atomic<bool> scanFinished{false};
  std::atomic<bool> candidatePending{false};
  std::atomic<bool> workerBusy{false};
  std::atomic<uint32_t> retryAtMs{0};

  char candidateAddress[18] = {};
  char candidateName[40] = {};
  uint8_t candidateAddressType = 0;

  NimBLEScan* scan = nullptr;
  NimBLEClient* client = nullptr;
  TaskHandle_t workerTask = nullptr;

  portMUX_TYPE eventMux = portMUX_INITIALIZER_UNLOCKED;
  char events[kEventSlots][kEventLength] = {};
  size_t eventHead = 0;
  size_t eventCount = 0;

  void enqueue(const char* fmt, ...) {
    char message[kEventLength];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(message, sizeof(message), fmt, ap);
    va_end(ap);

    portENTER_CRITICAL(&eventMux);
    const size_t slot = (eventHead + eventCount) % kEventSlots;
    strncpy(events[slot], message, kEventLength - 1);
    events[slot][kEventLength - 1] = '\0';
    if (eventCount < kEventSlots)
      ++eventCount;
    else
      eventHead = (eventHead + 1) % kEventSlots;
    portEXIT_CRITICAL(&eventMux);
  }

  bool dequeue(char* out) {
    bool haveEvent = false;
    portENTER_CRITICAL(&eventMux);
    if (eventCount) {
      strncpy(out, events[eventHead], kEventLength);
      out[kEventLength - 1] = '\0';
      eventHead = (eventHead + 1) % kEventSlots;
      --eventCount;
      haveEvent = true;
    }
    portEXIT_CRITICAL(&eventMux);
    return haveEvent;
  }

  void onResult(NimBLEAdvertisedDevice* device) override {
    if (candidatePending.load(std::memory_order_relaxed) || workerBusy.load()) return;
    if (!device->isAdvertisingService(NimBLEUUID(kHeartRateServiceUuid))) return;

    // NimBLE-Arduino 1.4.3 misreports legacy ADV_IND packets as non-connectable in this
    // configuration, so an advertised Heart Rate Service is sufficient to try the connection.
    const std::string address = device->getAddress().toString();
    const std::string name = device->haveName() ? device->getName() : std::string();

    strncpy(candidateAddress, address.c_str(), sizeof(candidateAddress) - 1);
    strncpy(candidateName, name.c_str(), sizeof(candidateName) - 1);
    candidateAddressType = device->getAddressType();
    candidatePending.store(true, std::memory_order_release);
    scan->stop();
    enqueue("BLE HR: found heart-rate sensor%s%s at %s",
            name.empty() ? "" : " ", name.empty() ? "" : name.c_str(), address.c_str());
    if (workerTask) xTaskNotifyGive(workerTask);
  }

  void onConnect(NimBLEClient*) override {
    linkConnected.store(true);
    enqueue("BLE HR: connected");
  }

  void onDisconnect(NimBLEClient*) override {
    const bool wasConnected = linkConnected.exchange(false);
    subscribed.store(false);
    retryAtMs = millis() + kRetryDelayMs;
    if (wasConnected) enqueue("BLE HR: disconnected; rescan in 5 seconds");
  }

  void scheduleRetry(const char* reason) {
    enqueue("BLE HR: %s; rescan in 5 seconds", reason);
    linkConnected.store(false);
    subscribed.store(false);
    retryAtMs = millis() + kRetryDelayMs;
  }

  static void notify(NimBLERemoteCharacteristic*, uint8_t* data, size_t length, bool) {
    if (!active) return;
    const HeartRateMeasurement measurement = parseHeartRateMeasurement(data, length);
    if (!measurement.valid) {
      active->enqueue("BLE HR: ignored malformed measurement (%u bytes)",
                      static_cast<unsigned>(length));
      return;
    }
    active->latestBpm.store(measurement.bpm, std::memory_order_relaxed);
    active->measurementSequence.fetch_add(1, std::memory_order_release);
  }

  void connectCandidate() {
    char addressText[sizeof(candidateAddress)];
    char name[sizeof(candidateName)];
    strncpy(addressText, candidateAddress, sizeof(addressText));
    strncpy(name, candidateName, sizeof(name));
    const uint8_t addressType = candidateAddressType;
    workerBusy.store(true);
    candidatePending.store(false, std::memory_order_release);

    enqueue("BLE HR: connecting to %s%s%s", name[0] ? name : addressText,
            name[0] ? " at " : "", name[0] ? addressText : "");

    if (client) {
      NimBLEDevice::deleteClient(client);
      client = nullptr;
    }
    client = NimBLEDevice::createClient(NimBLEAddress(std::string(addressText), addressType));
    if (!client) {
      scheduleRetry("could not allocate BLE client");
      workerBusy.store(false);
      return;
    }
    client->setClientCallbacks(this, false);
    client->setConnectTimeout(5);
    // Moderate intervals leave airtime for Wi-Fi while remaining far faster than heart-rate data.
    client->setConnectionParams(24, 40, 0, 100);
    if (!client->connect()) {
      NimBLEDevice::deleteClient(client);
      client = nullptr;
      scheduleRetry("connection failed");
      workerBusy.store(false);
      return;
    }

    NimBLERemoteService* service = client->getService(NimBLEUUID(kHeartRateServiceUuid));
    if (!service) {
      scheduleRetry("Heart Rate Service 0x180D not found");
      client->disconnect();
      workerBusy.store(false);
      return;
    }
    enqueue("BLE HR: Heart Rate Service discovered");

    NimBLERemoteCharacteristic* characteristic =
        service->getCharacteristic(NimBLEUUID(kHeartRateMeasurementUuid));
    if (!characteristic) {
      scheduleRetry("Heart Rate Measurement 0x2A37 not found");
      client->disconnect();
      workerBusy.store(false);
      return;
    }
    enqueue("BLE HR: Heart Rate Measurement discovered");

    const bool notifications = characteristic->canNotify();
    if ((!notifications && !characteristic->canIndicate()) ||
        !characteristic->subscribe(notifications, notify)) {
      scheduleRetry("notification subscription failed");
      client->disconnect();
      workerBusy.store(false);
      return;
    }
    subscribed.store(true);
    enqueue("BLE HR: subscribed");
    workerBusy.store(false);
  }

  static void taskEntry(void* arg) {
    Impl* self = static_cast<Impl*>(arg);
    for (;;) {
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500));
      if (self->candidatePending.load(std::memory_order_acquire) && !self->workerBusy.load())
        self->connectCandidate();
    }
  }

  static void scanComplete(NimBLEScanResults) {
    if (!active) return;
    active->scanning.store(false);
    if (!active->candidatePending.load() && !active->workerBusy.load())
      active->scanFinished.store(true);
  }

  void startScan() {
    scanFinished.store(false);
    scan->clearResults();
    scanning.store(true);
    enqueue("BLE HR: scanning");
    if (!scan->start(kScanSeconds, scanComplete, false)) {
      scanning.store(false);
      scheduleRetry("scan could not start");
    }
  }
};

BleHeartRateService::Impl* BleHeartRateService::Impl::active = nullptr;

void BleHeartRateService::begin() {
  if (impl_) return;
  impl_ = new Impl();
  Impl::active = impl_;

  NimBLEDevice::init("");
  impl_->scan = NimBLEDevice::getScan();
  impl_->scan->setAdvertisedDeviceCallbacks(impl_, false);
  impl_->scan->setActiveScan(true);
  impl_->scan->setInterval(60);
  impl_->scan->setWindow(60);
  impl_->scan->setMaxResults(0);
  if (xTaskCreatePinnedToCore(Impl::taskEntry, "blehr", kWorkerStackBytes, impl_,
                              kWorkerPriority, &impl_->workerTask, kWorkerCore) != pdPASS) {
    impl_->enqueue("BLE HR: worker task creation failed");
    impl_->scan = nullptr;
    return;
  }
  impl_->enqueue("BLE HR: initialized (%u KB free heap)",
                 static_cast<unsigned>(heap_caps_get_free_size(kGuardHeapCaps) / 1024));
}

void BleHeartRateService::tick(uint32_t nowMs) {
  if (!impl_) return;

  char event[kEventLength];
  while (impl_->dequeue(event)) logf("%s", event);
  if (!impl_->scan) return;

  static uint32_t loggedMeasurementSequence = 0;
  const uint32_t sequence = impl_->measurementSequence.load(std::memory_order_acquire);
  if (sequence != loggedMeasurementSequence) {
    loggedMeasurementSequence = sequence;
    logf("BLE HR: %u bpm", static_cast<unsigned>(impl_->latestBpm.load()));
  }

  if (impl_->scanFinished.exchange(false)) {
    impl_->retryAtMs = nowMs + kRetryDelayMs;
    logf("BLE HR: no sensor found; rescan in 5 seconds");
  }
  if (impl_->subscribed.load() || impl_->scanning.load() || impl_->workerBusy.load() ||
      impl_->candidatePending.load())
    return;
  if (static_cast<int32_t>(nowMs - impl_->retryAtMs.load()) >= 0) impl_->startScan();
}

bool BleHeartRateService::connected() const {
  // A link is not useful to scripts until Heart Rate Measurement notifications are active.
  return impl_ && impl_->subscribed.load();
}

bool BleHeartRateService::hasValidBpm() const {
  return impl_ && impl_->measurementSequence.load(std::memory_order_acquire) != 0;
}

uint16_t BleHeartRateService::bpm() const {
  return impl_ ? impl_->latestBpm.load() : 0;
}

}
}
