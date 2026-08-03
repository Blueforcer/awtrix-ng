#include "transport/mqtt/HaAnnouncer.h"

#include "AppConfig.h"
#include "hal/IBoard.h"
#include "system/Log.h"

namespace awtrix {

namespace {

class PubSubSink : public ha::IByteSink {
 public:
  explicit PubSubSink(PubSubClient& client) : client_(client) {}

  void write(const char* data, std::size_t len) override {
    client_.write(reinterpret_cast<const uint8_t*>(data), len);
  }

 private:
  PubSubClient& client_;
};

}

void HaAnnouncer::configure(const DeviceConfig& cfg, IBoard& board, const std::string& uid,
                            const std::string& prefix, const std::string& hostname) {
  enabled_ = cfg.haDiscovery;
  ISensorBus& sensors = board.sensors();
  ctx_.prefix = prefix;
  ctx_.haPrefix = cfg.haPrefix.empty() ? std::string("homeassistant") : cfg.haPrefix;
  ctx_.uid = uid;
  ctx_.hostname = hostname;
  ctx_.version = AWTRIX_NG_VERSION;
  ctx_.hasBattery = board.hasBattery();
  ctx_.hasLightSensor = board.hasLightSensor();
  ctx_.hasTemperature = sensors.hasSensor();
  ctx_.hasHumidity = sensors.hasHumidity();
  ctx_.hasPressure = sensors.hasPressure();
}

void HaAnnouncer::announce(PubSubClient& client) {
  const std::string topic = ha::discoveryTopic(ctx_);

  // The topic encodes the prefix, so a renamed device would otherwise leave a retained ghost entry
  // in Home Assistant. Clear the old one first.
  if (!announcedTopic_.empty() && announcedTopic_ != topic) {
    client.publish(announcedTopic_.c_str(), "", true);
    logf("mqtt: HA discovery retracted from %s", announcedTopic_.c_str());
    announcedTopic_.clear();
  }

  if (!enabled_) {
    client.publish(topic.c_str(), "", true);
    return;
  }

  // MQTT wants the payload length in the header, so the document is emitted twice: once to count
  // the bytes, once straight onto the wire. It is far too big to hold in RAM.
  ha::CountingSink counting;
  ha::emit(ctx_, counting);
  if (!client.beginPublish(topic.c_str(), static_cast<unsigned int>(counting.n), true)) {
    logf("mqtt: HA discovery header failed (%u bytes)", static_cast<unsigned>(counting.n));
    return;
  }
  PubSubSink sink(client);
  ha::emit(ctx_, sink);
  client.endPublish();
  announcedTopic_ = topic;
  logf("mqtt: HA discovery published to %s (%u bytes)", topic.c_str(),
       static_cast<unsigned>(counting.n));
}

}
