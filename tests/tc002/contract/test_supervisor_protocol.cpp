#include "../../support.h"
#include <cstdio>
#include <string>
#include <vector>

#include "platform/tc002/contract/SupervisorProtocol.h"
#include "platform/tc002/contract/Pcm16.h"

using namespace awtrix::tc002;

namespace {
int& failures = awtrix::test::failures();
using awtrix::test::check;
}

int main() {
  SupervisorMessage message;
  const std::vector<int16_t> signedSamples{-32768, -1, 0, 32767};
  check(encodePcm16(signedSamples) == "AID//wAA/38=", "PCM byte order and signed boundary vector");
  std::vector<int16_t> decoded{42};
  check(decodePcm16("AID//wAA/38=", decoded) && decoded == signedSamples, "PCM vector decode");
  check(!decodePcm16("AID//wAA/39=", decoded) && decoded == signedSamples, "noncanonical tail rejected without mutation");
  check(!decodePcm16("AA==", decoded), "odd byte count rejected");
  check(!decodePcm16("AAA", decoded), "missing canonical padding rejected");
  for (const char* malformed : {"====", "A===", "AA======", "AA=A", "ABAA====", "AB==", "AAB="}) {
    decoded = signedSamples;
    check(!decodePcm16(malformed, decoded) && decoded == signedSamples,
          "invalid padding and pad bits preserve the caller's PCM");
  }
  for (const std::vector<int16_t>& samples : {std::vector<int16_t>{42},
       std::vector<int16_t>{42, -123}, std::vector<int16_t>{42, -123, 32767}}) {
    check(decodePcm16(encodePcm16(samples), decoded) && decoded == samples,
          "all three canonical padding lengths round trip");
  }
  check(decodePcm16("", decoded) && decoded.empty(), "empty PCM for status event");

  check(encodeMicrophonePcmRequest(0).empty() && encodeMicrophonePcmRequest(-1).empty(),
        "PCM request IDs must be positive");
  MicrophonePcm pcm{7, std::vector<int16_t>(kMicrophonePcmSamples, -32768), {}};
  auto pcmWire = encodeMicrophonePcm(pcm);
  check(decodeSupervisorMessage(pcmWire, message) && message.microphonePcm.samples == pcm.samples,
        "signed PCM fits one datagram and round trips");
  const auto payloadAt = pcmWire.find("\"pcm\":\"") + 7;
  pcmWire[payloadAt] = '!';
  check(!decodeSupervisorMessage(pcmWire, message), "invalid base64 PCM rejected");
  pcmWire = encodeMicrophonePcm(pcm);
  pcmWire.erase(payloadAt, 4);
  check(!decodeSupervisorMessage(pcmWire, message), "short PCM cannot be used as a complete window");
  pcm.error = "busy";
  check(encodeMicrophonePcm(pcm).empty(), "error cannot carry partial samples");
  pcm.samples.clear();
  check(decodeSupervisorMessage(encodeMicrophonePcm(pcm), message) && message.microphonePcm.error == "busy",
        "PCM errors retain the request ID");
  check(!decodeSupervisorMessage("{\"v\":2,\"type\":\"microphonePcm\",\"id\":7,\"error\":\"busy\",\"pcm\":\"AAAA\"}", message),
        "received PCM error with samples rejected");
  check(!decodeSupervisorMessage("{\"v\":2,\"type\":\"microphonePcmRequest\",\"id\":1.5}", message),
        "fractional request IDs rejected");

  PowerStatus power;
  power.usbPower = true;
  power.batteryPercent = 76;
  power.batteryMillivolts = 4120;
  check(decodeSupervisorMessage(encodePower(power), message), "power decodes");
  check(message.type == MessageType::Power && message.power.usbPower &&
        message.power.batteryPercent == 76 && message.power.batteryMillivolts == 4120, "power round trip");

  NetworkStatus network;
  network.link = WifiLink::Connected;
  network.ssid = "Caf\xc3\xa9 \"5G\"";
  network.rssi = -61;
  network.mac = "02:00:00:00:00:07";
  network.ipv4 = "192.0.2.110";
  network.gateway = "192.0.2.1";
  network.dns = "192.0.2.1";
  network.hostname = "awtrix-000007";
  check(decodeSupervisorMessage(encodeNetwork(network), message), "network decodes");
  check(message.type == MessageType::Network && message.network.link == WifiLink::Connected &&
        message.network.ssid == network.ssid && message.network.rssi == -61 &&
        message.network.ipv4 == network.ipv4 && message.network.hostname == network.hostname,
        "network round trip");
  network.link = WifiLink::AccessPoint;
  check(decodeSupervisorMessage(encodeNetwork(network), message) &&
            message.network.link == WifiLink::AccessPoint &&
            std::string(wifiLinkName(message.network.link)) == "access-point", "access point round trip");

  TimeStatus time;
  time.synchronized = true;
  check(decodeSupervisorMessage(encodeTime(time), message) && message.time.synchronized, "time round trip");

  BluetoothStatus bluetooth;
  bluetooth.on = true;
  check(encodeBluetooth(bluetooth) == "{\"v\":2,\"type\":\"bluetooth\",\"on\":true,\"error\":\"\"}",
        "bluetooth wire form");
  bluetooth.on = false;
  bluetooth.error = "no answer from ttyS3";
  check(decodeSupervisorMessage(encodeBluetooth(bluetooth), message) && message.type == MessageType::Bluetooth &&
            !message.bluetooth.on && message.bluetooth.error == "no answer from ttyS3",
        "bluetooth round trip");
  bluetooth.error.assign(kMaxBluetoothError + 1, 'x');
  check(encodeBluetooth(bluetooth).empty(), "an overlong bluetooth error is not sent");
  check(!decodeSupervisorMessage("{\"v\":2,\"type\":\"bluetooth\",\"on\":\"yes\",\"error\":\"\"}", message),
        "bluetooth needs a boolean on");


  check(decodeSupervisorMessage(encodeWifi({"Home", "correct horse"}), message) &&
        message.type == MessageType::Wifi && message.wifi.ssid == "Home" &&
        message.wifi.password == "correct horse", "wifi round trip");
  check(decodeSupervisorMessage(encodeWifi({"Open", ""}), message) && message.wifi.password.empty(),
        "open network allowed");
  check(!decodeSupervisorMessage(encodeWifi({"", "x"}), message), "empty ssid refused");
  check(!decodeSupervisorMessage(encodeWifi({std::string(33, 'a'), "x"}), message), "long ssid refused");
  check(!decodeSupervisorMessage(encodeWifi({"a", std::string(65, 'b')}), message), "long password refused");
  check(decodeSupervisorMessage(encodeWifi({"Hex", std::string(64, 'b')}), message) &&
        message.wifi.password == std::string(64, 'b'), "64-digit hex PSK accepted");
  check(!decodeSupervisorMessage(encodeWifi({"a", std::string(63, 'b') + "g"}), message), "64 non-hex refused");
  check(!decodeSupervisorMessage(encodeWifi({"Home", "short"}), message), "passphrase under 8 characters refused");
  check(!decodeSupervisorMessage(encodeWifi({"Home", std::string("pass\x01word")}), message),
        "control character in a passphrase refused");
  check(checkWifiCredentials("Home", "") == WifiCredentialsProblem::None &&
            checkWifiCredentials("", "correct horse") == WifiCredentialsProblem::Ssid &&
            checkWifiCredentials("Home", "short") == WifiCredentialsProblem::Password,
        "one rule for SSID and password");

  check(decodeSupervisorMessage(encodeNtp("pool.ntp.org"), message) && message.ntpServer == "pool.ntp.org",
        "ntp round trip");
  check(decodeSupervisorMessage(encodeHostname("clock"), message) && message.hostname == "clock",
        "hostname round trip");
  StaticAddress address{true, "192.168.1.50", "255.255.255.0", "192.168.1.1", "", "1.1.1.1"};
  check(decodeSupervisorMessage(encodeAddress(address), message) && message.type == MessageType::Address &&
            message.address == address,
        "address round trip");
  check(decodeSupervisorMessage(encodeAddress(StaticAddress()), message) && !message.address.enabled,
        "DHCP round trip");
  check(!decodeSupervisorMessage(R"({"v":2,"type":"address","static":true,"ip":"192.168.1.50"})", message),
        "address without all fields refused");
  check(decodeSupervisorMessage(encodeReboot(), message) && message.type == MessageType::Reboot, "reboot");
  check(decodeSupervisorMessage(encodeHello("1.2.3"), message) && message.version == "1.2.3", "hello");

  check(!decodeSupervisorMessage("", message), "empty refused");
  check(!decodeSupervisorMessage("[]", message), "array refused");
  check(!decodeSupervisorMessage("{\"v\":3,\"type\":\"reboot\"}", message), "future version refused");
  check(!decodeSupervisorMessage("{\"v\":2,\"type\":\"unknown\"}", message), "unknown type refused");
  check(!decodeSupervisorMessage("{\"v\":2,\"type\":\"power\",\"usbPower\":1,\"batteryPercent\":5,"
                                 "\"batteryMillivolts\":1}", message), "non-bool refused");
  check(!decodeSupervisorMessage("{\"v\":2,\"type\":\"power\",\"usbPower\":true,\"batteryPercent\":101,"
                                 "\"batteryMillivolts\":1}", message), "percent range");
  check(message.type == MessageType::Invalid, "failed decode leaves an invalid message");
  check(!decodeSupervisorMessage(std::string(kMaxSupervisorMessage + 1, ' '), message), "oversize refused");

  check(decodeSupervisorMessage(encodeFactoryReset(), message) && message.type == MessageType::FactoryReset,
        "factory reset");
  check(encodeFactoryReset() == "{\"v\":2,\"type\":\"factoryReset\"}", "factory reset wire form");
  check(decodeSupervisorMessage(encodeWifiScan(), message) && message.type == MessageType::WifiScan, "wifi scan");
  check(encodeWifiScan() == "{\"v\":2,\"type\":\"wifiScan\"}", "wifi scan wire form");

  std::vector<WifiNetwork> networks{{"Caf\xc3\xa9 \"5G\"", -48, true}, {"", -90, false}, {"Guest", -71, false}};
  check(encodeWifiScanResult({{"Home", -52, true}}) ==
        "{\"v\":2,\"type\":\"wifiScanResult\",\"networks\":[{\"ssid\":\"Home\",\"rssi\":-52,\"secure\":true}]}",
        "scan result wire form");
  check(decodeSupervisorMessage(encodeWifiScanResult(networks), message) &&
        message.type == MessageType::WifiScanResult && message.networks.size() == 3 &&
        message.networks[0].ssid == networks[0].ssid && message.networks[0].rssi == -48 &&
        message.networks[0].secure && message.networks[1].ssid.empty() && !message.networks[2].secure,
        "scan result round trip keeps order, hidden and open networks");
  check(decodeSupervisorMessage(encodeWifiScanResult({}), message) && message.networks.empty(), "empty scan result");

  std::vector<WifiNetwork> many;
  for (int i = 0; i < 40; ++i) many.push_back({std::string(32, static_cast<char>('A' + i % 26)), -30 - i, i % 2 == 0});
  std::string full = encodeWifiScanResult(many);
  check(full.size() > 2048 && full.size() <= kMaxSupervisorMessage, "32 full-length SSIDs need the larger datagram");
  check(decodeSupervisorMessage(full, message) && message.networks.size() == kMaxWifiScanNetworks &&
        message.networks.back().rssi == -61, "at most 32 networks, the first ones kept");
  std::vector<WifiNetwork> escaped(32, WifiNetwork{std::string(32, '\x01'), -60, true});
  full = encodeWifiScanResult(escaped);
  check(!full.empty() && full.size() <= kMaxSupervisorMessage && decodeSupervisorMessage(full, message) &&
        !message.networks.empty() && message.networks.size() < 32, "escaped SSIDs are trimmed to one datagram");
  check(decodeSupervisorMessage(encodeWifiScanResult({{std::string(33, 'x'), -40, true}, {"ok", -250, true},
                                                      {"Valid", -40, false}}), message) &&
        message.networks.size() == 1 && message.networks[0].ssid == "Valid", "encoder drops invalid networks");

  const auto scan = [](const std::string& networksJson) {
    SupervisorMessage decoded;
    return decodeSupervisorMessage("{\"v\":2,\"type\":\"wifiScanResult\",\"networks\":" + networksJson + "}", decoded);
  };
  check(scan("[{\"ssid\":\"a\",\"rssi\":-1,\"secure\":false}]"), "minimal entry");
  check(!decodeSupervisorMessage("{\"v\":2,\"type\":\"wifiScanResult\"}", message), "networks required");
  check(message.type == MessageType::Invalid, "failed scan decode leaves an invalid message");
  check(!scan("{}") && !scan("null") && !scan("[1]") && !scan("[[]]"), "networks must be an array of objects");
  check(!scan("[{\"ssid\":\"a\",\"rssi\":-1}]"), "secure required");
  check(!scan("[{\"ssid\":\"a\",\"rssi\":-1,\"secure\":1}]"), "secure must be boolean");
  check(!scan("[{\"ssid\":\"a\",\"rssi\":1,\"secure\":true}]") && !scan("[{\"ssid\":\"a\",\"rssi\":-201,\"secure\":true}]") &&
        !scan("[{\"ssid\":\"a\",\"rssi\":-1.5,\"secure\":true}]"), "rssi range and type");
  check(!scan("[{\"ssid\":\"" + std::string(33, 'x') + "\",\"rssi\":-1,\"secure\":true}]"), "ssid length");
  check(!scan("[{\"ssid\":5,\"rssi\":-1,\"secure\":true}]"), "ssid must be a string");
  std::string tooMany = "[";
  for (int i = 0; i < 33; ++i) tooMany += std::string(i ? "," : "") + "{\"ssid\":\"n\",\"rssi\":-1,\"secure\":true}";
  check(!scan(tooMany + "]"), "more than 32 networks refused");
  check(!scan("[{\"ssid\":\"a\",\"rssi\":-1,\"secure\":true},]") && !scan("[{\"ssid\":\"a\",\"rssi\":-1,\"secure\":true}"),
        "malformed array refused");

  const RuntimeReady ready{"tc002", 52, 16, true};
  check(encodeReady(ready) == "{\"v\":2,\"type\":\"ready\",\"board\":\"tc002\",\"width\":52,\"height\":16,"
                              "\"input\":true}",
        "ready wire form");
  check(decodeSupervisorMessage(encodeReady(ready), message) && message.type == MessageType::Ready &&
            message.ready.board == "tc002" && message.ready.width == 52 && message.ready.height == 16 &&
            message.ready.input,
        "ready round trip");
  check(decodeSupervisorMessage(encodeReady({"headless", 64, 16, false}), message) && !message.ready.input,
        "a headless runtime reports ready without input");
  check(encodeReady({"headless", 52, 16, true}).empty() && encodeReady({"tc002", 52, 0, true}).empty() &&
            encodeReady({"esp32", 32, 8, false}).empty() && encodeReady({"tc002", 200, 16, true}).empty(),
        "input without the TC002, an empty or oversized frame and unknown boards are not encoded");
  check(!decodeSupervisorMessage("{\"v\":2,\"type\":\"ready\",\"board\":\"tc002\",\"width\":52,\"height\":16}",
                                 message),
        "ready needs input");
  check(!decodeSupervisorMessage("{\"v\":1,\"type\":\"reboot\"}", message), "protocol version 1 refused");

  check(encodeLog({"wifi", "association rejected: wrong key"}) ==
            "{\"v\":2,\"type\":\"log\",\"component\":\"wifi\",\"text\":\"association rejected: wrong key\"}",
        "log wire form");
  check(decodeSupervisorMessage(encodeLog({"update", "installing \"1.2\"\n"}), message) &&
            message.type == MessageType::Log && message.log.component == "update" &&
            message.log.text == "installing \"1.2\"\n",
        "log round trip keeps quotes and control characters");
  std::string wide;
  for (std::size_t i = 0; i < kMaxLogText; ++i) wide += "\xc3\xa4";
  check(decodeSupervisorMessage(encodeLog({"ip", wide}), message) && message.log.text.size() == kMaxLogText &&
            message.log.text == wide.substr(0, kMaxLogText),
        "a long text is cut to the limit at a character boundary");
  check(decodeSupervisorMessage(encodeLog({"ip", "x" + wide}), message) && message.log.text.size() == kMaxLogText - 1,
        "the cut backs off to the start of a two-byte character");
  check(encodeLog({"", "x"}).empty() && encodeLog({"Wifi", "x"}).empty() && encodeLog({"wifi0", "x"}).empty() &&
            encodeLog({std::string(kMaxLogComponent + 1, 'a'), "x"}).empty(),
        "components are lowercase letters and hyphens");
  check(!decodeSupervisorMessage("{\"v\":2,\"type\":\"log\",\"component\":\"wifi\",\"text\":\"" +
                                     std::string(kMaxLogText + 1, 'x') + "\"}",
                                 message),
        "an oversized text is refused");

  for (auto operation : {StreamControl::Probe, StreamControl::Start, StreamControl::Stop, StreamControl::Keepalive}) {
    check(decodeSupervisorMessage(encodeStreamControl({42, operation}), message) &&
      message.type == MessageType::MicrophoneStreamControl && message.streamControl.epoch == 42 &&
      message.streamControl.operation == operation, "stream control round trip");
  }
  StreamEvent stream;
  stream.epoch = 42; stream.kind = StreamEvent::Kind::Audio; stream.firstSample = 480;
  stream.hostAtMs = 123456; stream.capturedMs = 0xfffffff0;
  for (int i = 0; i < 480; ++i) stream.samples.push_back(static_cast<int16_t>(i * 137 - 32768));
  const auto encodedStream = encodeStreamEvent(stream);
  check(decodeSupervisorMessage(encodedStream, message) && message.type == MessageType::MicrophoneStreamEvent &&
    message.streamEvent.samples == stream.samples && message.streamEvent.firstSample == 480 &&
    message.streamEvent.hostAtMs == 123456, "stream PCM and monotonic timestamp round trip");
  stream.samples.push_back(1);
  check(encodeStreamEvent(stream).empty(), "oversized or partial DMA block rejected");
  check(!decodeSupervisorMessage("{\"v\":2,\"type\":\"microphoneStreamControl\",\"epoch\":0,\"operation\":1}", message),
        "zero stream epoch refused");
  if (failures) return 1;
  std::puts("supervisor protocol: ok");
  return 0;
}
