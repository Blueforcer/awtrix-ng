#include "platform_mqtt/Socket.h"

namespace awtrix {
void MqttSocket::begin(Options transport, const std::string& host) {
  transport_ = transport;
  if (transport_) transport_->setPeerName(host);
}

void MqttSocket::shutdown() {
  if (transport_) transport_->shutdown();
  else wifi_.stop();
  connecting_ = false;
}

Client& MqttSocket::socket() {
  return transport_ ? transport_->socket() : wifi_;
}

net::ResolveState MqttSocket::connectStep(std::function<bool()> connect) {
  const auto result = transport_ ? transport_->connectStep(std::move(connect))
                                : (connect() ? net::ResolveState::Ready : net::ResolveState::Failed);
  connecting_ = result == net::ResolveState::Pending;
  return result;
}
}
