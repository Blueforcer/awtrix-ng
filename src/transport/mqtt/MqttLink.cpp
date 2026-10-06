#include "transport/mqtt/MqttLink.h"

#include "system/Log.h"

namespace awtrix {

using net::LinkError;
using net::LinkPhase;
using net::ResolveState;

namespace {

// An inbound packet has to fit in here whole or PubSubClient drops it silently; large draw and
// notify commands are the reason it is this big. Outbound payloads are streamed and unaffected.
constexpr uint16_t kMqttBufferBytes = 8192;

// PubSubClient blocks the whole loop while it waits for CONNACK, so keep it short.
constexpr uint16_t kHandshakeSeconds = 2;

std::string endpointOf(const IPAddress& ip, uint16_t port) {
  std::string endpoint(ip.toString().c_str());
  endpoint += ':';
  endpoint += std::to_string(port);
  return endpoint;
}

}

MqttLink::~MqttLink() {
  socket_.shutdown();
  delete client_;
}

void MqttLink::begin(const DeviceConfig& cfg, const std::string& clientId,
                     const std::string& prefix, net::IHostResolver* resolver,
                     net::LinkStatus* status, MqttSocket::Options transport) {
  status_ = status;
  resolver_ = resolver;
  enabled_ = cfg.mqttEnabled;
  host_ = cfg.mqttHost;
  port_ = cfg.mqttPort;
  user_ = cfg.mqttUser;
  pass_ = cfg.mqttPass;
  prefix_ = prefix;
  clientId_ = clientId;

  status_->enabled = enabled_;
  status_->host = host_;
  status_->phase = enabled_ ? LinkPhase::Offline : LinkPhase::Disabled;

  if (!enabled_) return;

  socket_.begin(transport, host_);
  client_ = new PubSubClient(socket_.socket());
  if (!client_->setBufferSize(kMqttBufferBytes))
    logf("mqtt: could not allocate a %u-byte packet buffer; large commands will be dropped",
         static_cast<unsigned>(kMqttBufferBytes));
  client_->setSocketTimeout(kHandshakeSeconds);
  logf("mqtt: broker %s:%u, prefix %s", host_.c_str(), port_, prefix_.c_str());
}

// Drives one step of the connect state machine and returns whether the link is usable right now.
// Everything here is non-blocking except the connect attempt itself.
bool MqttLink::tick(uint32_t nowMs, bool networkConnected) {
  if (!client_) return false;

  if (!networkConnected) {
    if (sawWifi_) {
      socket_.shutdown();
      wasConnected_ = false;
    }
    sawWifi_ = false;
    haveServer_ = false;
    status_->phase = LinkPhase::Offline;
    status_->setError(LinkError::NoWifi);
    status_->retryInMs = 0;
    return false;
  }

  // Fresh Wi-Fi association: the broker may well have a different address now, and the user should
  // not wait out a backoff that was earned while offline.
  if (!sawWifi_) {
    sawWifi_ = true;
    resolver_->forget();
    haveServer_ = false;
    backoff_.reset();
  }

  if (!socket_.connecting() && client_->connected()) {
    client_->loop();
    status_->retryInMs = 0;
    return true;
  }

  if (wasConnected_) {
    wasConnected_ = false;
    const int state = client_->state();
    logf("mqtt: connection to %s lost (state %d)", status_->endpoint.c_str(), state);
    status_->phase = LinkPhase::Offline;
    status_->setError(LinkError::Lost);
    loggedError_ = LinkError::Lost;
  }

  if (!backoff_.due(nowMs)) {
    status_->retryInMs = backoff_.retryInMs(nowMs);
    return false;
  }

  if (!haveServer_) {
    status_->phase = LinkPhase::Connecting;
    status_->retryInMs = 0;
    const ResolveState resolved = resolver_->resolve(host_);
    if (resolved == ResolveState::Pending) return false;
    if (resolved == ResolveState::Failed) {
      noteFailure(nowMs, resolver_->error(), 0);
      return false;
    }
    const uint32_t resolvedAddress = resolver_->address();
    const IPAddress address(resolvedAddress >> 24, resolvedAddress >> 16, resolvedAddress >> 8, resolvedAddress);
    client_->setServer(address, port_);
    status_->endpoint = endpointOf(address, port_);
    haveServer_ = true;
  }

  status_->phase = LinkPhase::Connecting;
  const auto result = socket_.connectStep([this] { return connectNow(); });
  if (result == ResolveState::Pending) return false;
  if (result == ResolveState::Failed) {
    const int state = client_->state();
    // A cached address that keeps refusing is usually stale (broker moved, DHCP lease changed), so
    // drop it and resolve again rather than retrying the same IP forever.
    if (++failuresAgainstAddress_ >= kReresolveAfterFailures) {
      failuresAgainstAddress_ = 0;
      resolver_->forget();
      haveServer_ = false;
    }
    noteFailure(nowMs, mapState(state), state);
    return false;
  }

  backoff_.onSuccess();
  failuresAgainstAddress_ = 0;
  wasConnected_ = true;
  loggedError_ = LinkError::None;
  status_->phase = LinkPhase::Connected;
  status_->setError(LinkError::None);
  status_->attempts = 0;
  status_->retryInMs = 0;
  ++status_->connects;
  logf("mqtt: connected to %s (%s), prefix %s", host_.c_str(), status_->endpoint.c_str(),
       prefix_.c_str());
  if (onOnline_) onOnline_();
  return true;
}

// Registers a retained "offline" will, so the broker publishes it for us if the device drops off
// without saying goodbye.
bool MqttLink::connectNow() {
  const std::string will = prefix_ + "/availability";
  return (user_.empty() && pass_.empty())
             ? client_->connect(clientId_.c_str(), will.c_str(), 0, true, "offline")
             : client_->connect(clientId_.c_str(), user_.c_str(), pass_.c_str(), will.c_str(), 0,
                                true, "offline");
}

void MqttLink::noteFailure(uint32_t nowMs, LinkError why, int state) {
  const uint32_t delay = backoff_.onFailure(nowMs);
  backoff_.arm(nowMs);
  status_->phase = LinkPhase::Offline;
  status_->setError(why);
  status_->attempts = backoff_.attempts();
  status_->retryInMs = delay;

  // Log a change of cause immediately, otherwise only every tenth attempt, so an unreachable broker
  // cannot fill the log ring.
  if (why != loggedError_ || (backoff_.attempts() % 10) == 1) {
    if (state != 0)
      logf("mqtt: connect to %s failed (%s, state %d), retry in %u s", status_->endpoint.c_str(),
           net::linkErrorName(why), state, static_cast<unsigned>(delay / 1000));
    else
      logf("mqtt: could not resolve %s (%s), retry in %u s", host_.c_str(),
           net::linkErrorName(why), static_cast<unsigned>(delay / 1000));
  }
  loggedError_ = why;
}

LinkError MqttLink::mapState(int pubSubState) {
  switch (pubSubState) {
    case MQTT_CONNECTION_TIMEOUT:      return LinkError::Timeout;
    case MQTT_CONNECTION_LOST:         return LinkError::Lost;
    case MQTT_CONNECT_FAILED:          return LinkError::Refused;
    case MQTT_DISCONNECTED:            return LinkError::Refused;
    case MQTT_CONNECT_BAD_PROTOCOL:    return LinkError::Rejected;
    case MQTT_CONNECT_BAD_CLIENT_ID:   return LinkError::Rejected;
    case MQTT_CONNECT_UNAVAILABLE:     return LinkError::Rejected;
    case MQTT_CONNECT_BAD_CREDENTIALS: return LinkError::BadCredentials;
    case MQTT_CONNECT_UNAUTHORIZED:    return LinkError::BadCredentials;
    default:                           return LinkError::Refused;
  }
}

}
