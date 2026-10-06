#pragma once

#include <functional>
#include <string>

#include "platform/tc002/daemon/Service.h"

namespace awtrix::tc002d {

struct RuntimeLinks {
  WifiControl* wifi = nullptr;
  TimeControl* time = nullptr;
  HostnameControl* hostname = nullptr;
  AddressControl* address = nullptr;
  std::function<void()> reboot;
  std::function<void(const tc002::UpdateReady& ready)> updateReady;
  // The supervisor's hello datagram, sent first after the runtime's hello and by sendHello().
  std::function<std::string()> hello;
  // The runtime wants the Bluetooth controller attached or released; released too when it ends.
  std::function<void(bool on)> bluetooth;
  using PcmReply = std::function<void(const tc002::MicrophonePcm&)>;
  std::function<void(int id, PcmReply reply)> microphonePcm;
  using StreamReply = std::function<bool(const tc002::StreamEvent&)>;
  std::function<bool()> microphoneStreamAvailable;
  std::function<bool(int epoch, StreamReply reply)> microphoneStreamStart;
  std::function<void(int epoch, bool stop)> microphoneStreamControl;
};

}  // namespace awtrix::tc002d
