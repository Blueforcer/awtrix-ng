#include "../support.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>

#include "platform/linux/ble/AdvData.h"
#include "platform/linux/ble/Att.h"
#include "platform/linux/ble/BleHub.h"
#include "platform/linux/ble/Gamepad.h"
#include "platform/linux/ble/GamepadManager.h"
#include "platform/linux/ble/GattClient.h"
#include "platform/linux/ble/GattServer.h"
#include "platform/linux/ble/HidGamepad.h"
#include "platform/linux/ble/LinuxBleRadio.h"
#include "platform/posix/Files.h"
#include "FakeBleRadio.h"

using namespace awtrix;
using namespace awtrix::ble;
using namespace awtrix::ble::testing;

namespace {

int& failures = awtrix::test::failures();

using awtrix::test::check;

bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

struct Events {
  std::vector<BleEvent> all;
  std::vector<BleEvent> of(const std::string& script, uint32_t id) const {
    std::vector<BleEvent> out;
    for (const auto& e : all)
      if (e.script == script && e.id == id) out.push_back(e);
    return out;
  }
  std::string last(const std::string& script, uint32_t id) const {
    auto v = of(script, id);
    return v.empty() ? std::string() : v.back().json;
  }
};

Bytes advWith(const char* name, uint16_t service) {
  AdvPayload p;
  p.name = name;
  p.uuids.push_back(Uuid::from16(service));
  Bytes adv, rsp;
  std::string e;
  buildAdv(p, adv, rsp, e);
  return adv;
}

void types() {
  check(uuid("180D").str() == "180d", "a SIG number prints short");
  check(uuid("0x2A37") == Uuid::from16(0x2a37), "a 0x prefix is accepted");
  check(uuid("0000180d-0000-1000-8000-00805f9b34fb") == Uuid::from16(0x180d), "the long form of a SIG number is the same");
  const Uuid custom = uuid("6e400001-b5a3-f393-e0a9-e50e24dcca9e");
  check(!custom.isShort() && custom.str() == "6e400001-b5a3-f393-e0a9-e50e24dcca9e", "a vendor uuid round-trips");
  Bytes wire;
  custom.appendWire(wire);
  Uuid back;
  check(Uuid::fromWire(wire.data(), wire.size(), back) && back == custom, "wire order round-trips");
  Uuid bad;
  check(!Uuid::parse("18", bad) && !Uuid::parse("xyz1", bad), "junk is no uuid");
  check(mkaddr("AA:BB:CC:DD:EE:01").str() == "AA:BB:CC:DD:EE:01", "an address round-trips");
  Address a;
  check(!Address::parse("AA:BB:CC:DD:EE", false, a), "a short address is refused");
  Bytes h;
  check(fromHex("00ff10", h) && h == Bytes({0x00, 0xff, 0x10}) && toHex(h) == "00ff10", "hex round-trips");
  check(!fromHex("0", h) && !fromHex("zz", h), "odd or non-hex text is refused");
}

void advertising() {
  const Bytes sample = {0x02, 0x01, 0x06, 0x03, 0x03, 0x0d, 0x18, 0x05, 0x09, 'N', 'G', '-', 'P',
                        0x07, 0xff, 0x4c, 0x00, 0x01, 0x02, 0x03, 0x04, 0x04, 0x16, 0xaa, 0xfe, 0x42};
  const AdvFields f = parseAdv(sample);
  check(f.hasFlags && f.flags == 6, "flags are read");
  check(f.uuids.size() == 1 && f.uuids[0] == Uuid::from16(0x180d), "a 16-bit service list is read");
  check(f.name == "NG-P" && f.completeName, "the complete name is read");
  check(f.manufacturer.size() == 1 && f.manufacturer[0].first == 0x004c &&
            f.manufacturer[0].second == Bytes({1, 2, 3, 4}),
        "manufacturer data is split into company and payload");
  check(f.serviceData.size() == 1 && f.serviceData[0].first == Uuid::from16(0xfeaa) &&
            f.serviceData[0].second == Bytes({0x42}),
        "service data is keyed by its uuid");
  const Bytes truncated = {0x02, 0x01, 0x06, 0x09, 0x09, 'a'};
  check(parseAdv(truncated).hasFlags && parseAdv(truncated).name.empty(), "a field running past the end is dropped");

  AdvPayload p;
  p.name = "AWTRIX-TC002";
  p.uuids = {Uuid::from16(0x180d)};
  p.manufacturer = {{0xffff, Bytes({'N', 'G'})}};
  Bytes adv, rsp;
  std::string e;
  check(buildAdv(p, adv, rsp, e), "a small payload builds");
  const AdvFields back = parseAdv(adv);
  check(adv.size() <= kAdvMax - kAdvFlagsCost && back.name == "AWTRIX-TC002" && back.uuids.size() == 1,
        "everything fits the advertisement itself");
  p.uuids.push_back(uuid("6e400001-b5a3-f393-e0a9-e50e24dcca9e"));
  check(buildAdv(p, adv, rsp, e), "a vendor uuid still builds");
  Bytes both = adv;
  both.insert(both.end(), rsp.begin(), rsp.end());
  const AdvFields spilled = parseAdv(both);
  check(!rsp.empty() && spilled.uuids.size() == 2 && spilled.name == "AWTRIX-TC002", "fields spill into the scan response");
  AdvPayload asking;
  asking.solicit = {uuid("7905f431-b5ce-4e99-a40f-4b1e122d00d0")};
  check(buildAdv(asking, adv, rsp, e) && adv.size() == 18 && adv[1] == 0x15 && adv[2] == 0xd0 && adv[17] == 0x79,
        "a solicited service goes out as a 128-bit solicitation, wire order");
  AdvPayload huge;
  huge.manufacturer = {{1, Bytes(40, 0)}};
  check(!buildAdv(huge, adv, rsp, e) && !e.empty(), "a field longer than a packet is refused");
  AdvPayload longName;
  longName.name = std::string(60, 'x');
  check(buildAdv(longName, adv, rsp, e), "a long name is shortened, not refused");
  const AdvFields cut = parseAdv(adv.size() >= rsp.size() ? adv : rsp);
  check(!cut.completeName && !cut.name.empty(), "the shortened name is marked short");
}

// A client and a server talking to each other directly: discovery, read, write, notify.
void clientAgainstServer() {
  GattServer server;
  server.setDeviceName("PEER");
  std::vector<uint16_t> handles;
  const int svc = server.addService(uuid("fff0"),
                                    {{uuid("fff1"), static_cast<uint8_t>(att::kPropRead | att::kPropWrite | att::kPropNotify), false, {'h', 'i'}, {}},
                                     {uuid("fff2"), att::kPropRead, true, {'s'}, {}},
                                     {uuid("fff3"), att::kPropRead, false, Bytes(300, 7), {}}},
                                    handles);
  check(svc > 0 && handles.size() == 3, "a service with three characteristics is added");
  server.linkUp(1);
  std::deque<Bytes> toServer, toClient;
  GattClient client([&](const Bytes& pdu) {
    toServer.push_back(pdu);
    return true;
  });
  bool ready = false;
  std::vector<Bytes> values;
  client.onReady = [&](bool ok) { ready = ok; };
  client.onValue = [&](uint16_t, const Bytes& v) { values.push_back(v); };
  int level = 1;
  auto run = [&] {
    for (int guard = 0; guard < 500 && (!toServer.empty() || !toClient.empty()); ++guard) {
      if (!toServer.empty()) {
        const Bytes pdu = toServer.front();
        toServer.pop_front();
        const Bytes rsp = server.handle(1, pdu.data(), pdu.size(), level);
        if (!rsp.empty()) toClient.push_back(rsp);
        for (auto& [l, p] : server.drain()) toClient.push_back(p);
      }
      if (!toClient.empty()) {
        const Bytes pdu = toClient.front();
        toClient.pop_front();
        client.receive(pdu.data(), pdu.size(), 0);
      }
    }
  };
  client.start(0);
  run();
  check(ready, "discovery finishes");
  check(client.mtu() == att::kMaxMtu, "both sides agree on the largest MTU");
  check(client.services().size() == 3, "GAP, GATT and the script's service are found");
  const GattClient::Characteristic* c = client.find(uuid("fff0"), uuid("fff1"));
  check(c && c->value == handles[0] && c->clientConfig == handles[0] + 1, "the notify characteristic and its CCCD are found");
  Bytes got;
  uint8_t code = 0xff;
  client.read(handles[0], [&](bool ok, uint8_t e, const Bytes& v) { got = ok ? v : Bytes(); code = e; });
  run();
  check(got == Bytes({'h', 'i'}), "a read returns the stored value");
  client.read(handles[2], [&](bool ok, uint8_t, const Bytes& v) { got = ok ? v : Bytes(); });
  run();
  check(got == Bytes(300, 7), "a long value is completed with blob reads");
  client.read(handles[1], [&](bool ok, uint8_t e, const Bytes&) { code = ok ? 0 : e; });
  run();
  check(code == att::kInsufficientAuthentication, "an encrypted value asks for pairing on a plain link");
  level = 2;
  client.read(handles[1], [&](bool ok, uint8_t e, const Bytes&) { code = ok ? 0 : e; });
  run();
  check(code == 0, "the same value reads once the link is encrypted");
  uint16_t written = 0;
  Bytes writtenValue;
  server.onWrite = [&](int, uint16_t h, const Bytes& v) { written = h, writtenValue = v; };
  bool wrote = false;
  client.write(handles[0], {0xca, 0xfe}, true, [&](bool ok, uint8_t, const Bytes&) { wrote = ok; });
  run();
  check(wrote && written == handles[0] && writtenValue == Bytes({0xca, 0xfe}), "a write reaches the server");
  client.write(c->clientConfig, {1, 0}, true, [](bool, uint8_t, const Bytes&) {});
  run();
  server.setValue(handles[0], {9});
  server.publish(handles[0]);
  for (auto& [l, p] : server.drain()) toClient.push_back(p);
  run();
  check(values.size() == 1 && values[0] == Bytes({9}), "a subscribed value arrives as a notification");
  std::vector<uint16_t> more;
  server.addService(uuid("fff9"), {{uuid("fff8"), att::kPropRead, false, {}, {}}}, more);
  const auto pending = server.drain();
  check(pending.empty(), "no service-changed indication without a subscription to it");
}

void encryptedSubscriptions() {
  for (uint16_t config : {1, 2}) {
    GattServer server;
    std::vector<uint16_t> handles;
    const int service = server.addService(uuid("fff0"),
        {{uuid("fff1"), static_cast<uint8_t>(att::kPropRead | att::kPropNotify | att::kPropIndicate), true, {42}, {}},
         {uuid("fff2"), att::kPropNotify, false, {7}, {}}}, handles);
    server.linkUp(1);
    server.linkUp(2);
    const auto subscribe = [&](int link, uint16_t handle, uint16_t value, int level) {
      Bytes request{att::kWriteReq};
      put16(request, handle + 1);
      put16(request, value);
      return server.handle(link, request.data(), request.size(), level);
    };
    const auto allowed = [](int link) { return link == 1 ? 2 : 1; };
    check(subscribe(1, handles[0], config, 1) ==
              att::error(att::kWriteReq, handles[0] + 1, att::kInsufficientAuthentication),
          "encrypted subscriptions reject a plain link");
    server.publish(handles[0]);
    check(server.drain(allowed).empty(), "a rejected subscription receives no value");
    check(subscribe(1, handles[0], config, 2) == Bytes{att::kWriteRsp},
          "an encrypted link can subscribe");
    server.publish(handles[0]);
    auto sent = server.drain(allowed);
    check(sent.size() == 1 && sent[0].first == 1 && sent[0].second.back() == 42,
          "an encrypted subscriber receives the protected value");
    const uint8_t confirm = att::kConfirm;
    server.handle(1, &confirm, 1, 2);
    server.publish(handles[0]);
    subscribe(2, handles[1], 1, 1);
    server.publish(handles[1]);
    sent = server.drain([](int) { return 1; });
    check(sent.size() == 1 && sent[0].first == 2 && sent[0].second.back() == 7,
          "delivery checks current encryption without suppressing unprotected peers");
    server.publish(handles[0]);
    check(server.drain(allowed).size() == 1,
          "a discarded indication does not wait for confirmation");
    server.handle(1, &confirm, 1, 2);
    server.publish(handles[0]);
    check(server.drain().empty(), "protected delivery fails closed without link security");
    server.publish(handles[0]);
    server.removeService(service);
    check(server.drain(allowed).empty(), "removed values are not delivered from an old queue");
  }
}

void persistentBonds() {
  char directory[] = "/tmp/awtrix-ble-bonds-XXXXXX";
  const char* made = ::mkdtemp(directory);
  check(made != nullptr, "bond fixture gets a private directory");
  if (!made) return;
  const std::string path = std::string(directory) + "/bonds.json";
  const Address first = mkaddr("02:00:00:00:00:11");
  const Address second = mkaddr("02:00:00:00:00:12");
  const auto key = [](const Address& peer) {
    Bytes bytes(36, 0);
    std::copy(peer.b.begin(), peer.b.end(), bytes.begin());
    bytes[6] = 1;
    return toHex(bytes);
  };
  {
    std::ofstream seed(path);
    seed << "{\"ltk\":[\"" << key(first) << "\",\"" << key(second) << "\"],\"irk\":[]}";
  }
  LinuxBleRadio::Options options;
  options.bondsPath = path;
  {
    LinuxBleRadio radio(options);
    check(radio.bonds().size() == 2, "existing pairing state loads without a controller");
    check(::unlink(path.c_str()) == 0, "fixture removes the old pairing store");
    check(radio.forget(first), "a changed pairing set is persisted to a missing store");
    struct stat state{};
    check(::stat(path.c_str(), &state) == 0 && (state.st_mode & 0777) == 0600,
          "a newly created pairing store is private");
  }
  {
    LinuxBleRadio restored(options);
    const auto bonds = restored.bonds();
    check(bonds.size() == 1 && bonds[0] == second, "remaining pairing keys survive reload");
    check(restored.forget(second), "the final pairing can be removed");
  }
  {
    LinuxBleRadio empty(options);
    check(empty.bonds().empty(), "an empty pairing set replaces the previous contents");
  }
  ::unlink(path.c_str());
  check(::rmdir(directory) == 0, "atomic pairing writes leave no temporary files");
}

struct Rig {
  FakeRadio radio;
  Events events;
  int64_t now = 1000;
  BleHub hub{radio, [this](BleEvent e) { events.all.push_back(std::move(e)); }};
  std::string call(const char* script, const char* op, const std::string& args, uint32_t id) {
    std::string r = hub.call(script, op, args, id, now);
    radio.pump(hub, now);
    return r;
  }
  void settle() { radio.pump(hub, now); }
};

void sharedScan() {
  Rig r;
  check(r.hub.power() == Power::Off, "the controller starts off");
  check(r.call("a", "scan", R"({"uuid":"180d"})", 1) == "{\"ok\":true}", "a first scan is accepted before power is up");
  check(r.radio.powerCalls == std::vector<bool>({true}), "the first user switches the controller on");
  check(r.radio.scanning && !r.radio.scanActive, "the queued scan starts once powered, passive");
  r.call("b", "scan", R"({"name":"Govee","active":true,"dedupe":0})", 7);
  check(r.radio.scanActive, "one script asking for an active scan makes the shared scan active");
  r.radio.advert("AA:BB:CC:DD:EE:01", 0, -50, advWith("HR", 0x180d));
  r.radio.advert("AA:BB:CC:DD:EE:02", 3, -70, advWith("Govee_H5075", 0xec88));
  check(r.events.of("a", 1).size() == 1 && has(r.events.last("a", 1), "\"uuids\":[\"180d\"]"),
        "a sees only the device with its service");
  check(r.events.of("b", 7).size() == 1 && has(r.events.last("b", 7), "Govee_H5075") &&
            has(r.events.last("b", 7), "\"connectable\":false"),
        "b sees only the device with its name");
  r.radio.advert("AA:BB:CC:DD:EE:01", 0, -51, advWith("HR", 0x180d));
  r.radio.advert("AA:BB:CC:DD:EE:02", 3, -71, advWith("Govee_H5075", 0xec88));
  check(r.events.of("a", 1).size() == 1, "a's default dedupe holds a repeat within a second");
  check(r.events.of("b", 7).size() == 2, "b asked for every report");
  r.now += 1500;
  r.hub.tick(r.now);
  r.radio.advert("AA:BB:CC:DD:EE:01", 0, -52, advWith("HR", 0x180d));
  check(r.events.of("a", 1).size() == 2, "after the dedupe window a report passes again");
  r.hub.forget("b", r.now);
  check(r.radio.scanning && !r.radio.scanActive, "removing b leaves a's passive scan running");
  check(r.call("a", "stop", R"({"id":1})", 99) == "{\"ok\":true}" && !r.radio.scanning, "stopping the last scan stops the controller's");
  check(r.events.of("a", 1).back().done, "a's scan callback is told it ended");
  for (int i = 0; i < 4; ++i) r.call("c", "scan", "{}", static_cast<uint32_t>(20 + i));
  check(has(r.call("c", "scan", "{}", 30), "too many scans"), "the per-script scan limit holds");
}

void rateLimit() {
  Rig r;
  r.call("a", "scan", R"({"dedupe":0})", 1);
  char addr[18];
  for (int i = 0; i < 60; ++i) {
    std::snprintf(addr, sizeof addr, "AA:BB:CC:DD:%02X:%02X", i / 256, i % 256);
    r.radio.advert(addr, 0, -40, advWith("x", 0x1234));
  }
  const auto delivered = r.events.of("a", 1).size();
  check(delivered > 0 && delivered < 60, "a burst is capped at the event budget");
  r.now += 1000;
  r.hub.tick(r.now);
  r.radio.advert("AA:BB:CC:DD:EE:FF", 0, -40, advWith("x", 0x1234));
  check(r.events.of("a", 1).size() == delivered + 1, "the budget refills");
}

void sharedConnection() {
  Rig r;
  std::vector<uint16_t> handles;
  r.radio.peerDb.addService(Uuid::from16(0x180d),
                            {{Uuid::from16(0x2a37), att::kPropNotify, false, {0, 60}, {}},
                             {Uuid::from16(0x2a38), att::kPropRead, false, {1}, {}}},
                            handles);
  r.radio.peerDb.drain();
  const std::string hr = R"({"addr":"C4:DE:E2:1F:D1:B6"})";
  r.call("a", "connect", hr, 10);
  r.call("b", "connect", hr, 20);
  check(r.radio.connectCalls == 1, "two scripts on one peer share one link");
  check(has(r.events.last("a", 10), "\"connected\":true") && has(r.events.last("b", 20), "\"connected\":true"),
        "both scripts hear the link is ready");
  check(has(r.call("a", "services", R"({"conn":10})", 0), "\"uuid\":\"180d\""), "services are listed from the cache");
  r.call("a", "read", R"({"conn":10,"svc":"180d","chr":"2a38"})", 11);
  check(r.events.last("a", 11) == R"({"data":"01"})" && r.events.of("a", 11).back().done, "a read answers once and ends");
  r.call("a", "subscribe", R"({"conn":10,"svc":"180d","chr":"2a37"})", 12);
  r.call("b", "subscribe", R"({"conn":20,"svc":"180d","chr":"2a37"})", 22);
  int cccdWrites = 0;
  for (const auto& [link, pdu] : r.radio.sent)
    if (pdu.size() == 5 && pdu[0] == att::kWriteReq && le16(pdu.data() + 1) == handles[0] + 1) ++cccdWrites;
  check(cccdWrites == 1, "the second subscriber reuses the peer's notifications");
  r.radio.notifyPeer(handles[0], {0, 72});
  r.settle();
  check(r.events.last("a", 12) == R"({"data":"0048"})" && r.events.last("b", 22) == R"({"data":"0048"})",
        "a notification reaches every subscriber");
  r.hub.forget("a", r.now);
  r.settle();
  check(r.radio.disconnected.empty(), "b keeps the link when a goes");
  r.radio.notifyPeer(handles[0], {0, 73});
  r.settle();
  check(r.events.last("b", 22) == R"({"data":"0049"})" && r.events.of("a", 12).size() == 1, "only b hears the next beat");
  r.call("b", "stop", R"({"id":22})", 0);
  bool offWritten = false;
  for (const auto& [link, pdu] : r.radio.sent)
    if (pdu.size() == 5 && pdu[0] == att::kWriteReq && le16(pdu.data() + 1) == handles[0] + 1 && pdu[3] == 0) offWritten = true;
  check(offWritten, "the last subscriber leaving switches the peer's notifications off");
  r.call("b", "disconnect", R"({"conn":20})", 0);
  check(r.radio.disconnected.size() == 1, "the last user closes the link");
  check(has(r.events.last("b", 20), "\"disconnected\":true") && r.events.of("b", 20).back().done,
        "the connect callback ends with the link");
}

// A gamepad streams far more reports than a script can use: with an interval the script gets the
// newest one at most that often, and the last of a burst once the interval is over.
void thinnedSubscription() {
  Rig r;
  std::vector<uint16_t> handles;
  r.radio.peerDb.addService(Uuid::from16(0x1812), {{Uuid::from16(0x2a4d), att::kPropNotify, false, {}, {}}}, handles);
  r.radio.peerDb.drain();
  r.call("a", "connect", R"({"addr":"E4:17:D8:BC:EC:1D"})", 1);
  r.call("a", "subscribe", R"({"conn":1,"svc":"1812","chr":"2a4d","interval":50})", 2);
  r.call("b", "connect", R"({"addr":"E4:17:D8:BC:EC:1D"})", 3);
  r.call("b", "subscribe", R"({"conn":3,"svc":"1812","chr":"2a4d"})", 4);
  for (uint8_t i = 1; i <= 5; ++i) {
    r.now += 5;
    r.hub.tick(r.now);
    r.radio.notifyPeer(handles[0], {i});
    r.settle();
  }
  check(r.events.of("a", 2).size() == 1 && r.events.last("a", 2) == R"({"data":"01"})",
        "a thinned subscription passes the first value of a burst");
  check(r.events.of("b", 4).size() == 5, "an unthinned one on the same link gets every value");
  r.now += 50;
  r.hub.tick(r.now);
  check(r.events.of("a", 2).size() == 2 && r.events.last("a", 2) == R"({"data":"05"})",
        "the newest value follows once the interval is over");
  r.now += 100;
  r.hub.tick(r.now);
  check(r.events.of("a", 2).size() == 2, "and nothing is sent twice");
}

// A HID device keeps its reports for paired centrals: the configuration write is refused until the
// link is encrypted, so subscribing pairs and writes it once more.
void subscribePairsWhenAsked() {
  Rig r;
  std::vector<uint16_t> handles;
  r.radio.peerDb.addService(Uuid::from16(0x1812), {{Uuid::from16(0x2a4d), att::kPropNotify, false, {}, {}}}, handles);
  r.radio.peerDb.drain();
  r.radio.lockedHandle = handles[0] + 1;
  r.call("a", "connect", R"({"addr":"E4:17:D8:BC:EC:1D"})", 1);
  r.call("a", "subscribe", R"({"conn":1,"svc":"1812","chr":"2a4d"})", 2);
  check(r.radio.levels.rbegin()->second == 2, "the link was paired for the subscription");
  check(r.events.of("a", 2).empty(), "without an error for the script");
  r.radio.notifyPeer(handles[0], {0x0f, 0x7f});
  r.settle();
  check(r.events.last("a", 2) == R"({"data":"0f7f"})", "and the reports arrive");
}

// Two 180d instances: subscriptions listen to both; reads use the readable instance.
void duplicateServices() {
  Rig r;
  std::vector<uint16_t> stale, live;
  r.radio.peerDb.addService(Uuid::from16(0x180d), {{Uuid::from16(0x2a37), att::kPropNotify, false, {}, {}}}, stale);
  r.radio.peerDb.addService(Uuid::from16(0x180d),
                            {{Uuid::from16(0x2a37), att::kPropNotify, false, {}, {}},
                             {Uuid::from16(0x2a38), att::kPropRead, false, {2}, {}}},
                            live);
  r.radio.peerDb.drain();
  r.call("a", "connect", R"({"addr":"4C:A9:CD:53:5D:85","random":true})", 1);
  check(has(r.events.last("a", 1), "\"connected\":true"), "the watch connects");
  r.call("a", "subscribe", R"({"conn":1,"svc":"180d","chr":"2a37"})", 2);
  r.radio.notifyPeer(live[0], {0, 91});
  r.settle();
  check(r.events.last("a", 2) == R"({"data":"005b"})", "the second instance's beats arrive");
  r.radio.notifyPeer(stale[0], {0, 50});
  r.settle();
  check(r.events.last("a", 2) == R"({"data":"0032"})", "and the first one's too, should it ever send");
  r.call("a", "read", R"({"conn":1,"svc":"180d","chr":"2a38"})", 3);
  check(r.events.last("a", 3) == R"({"data":"02"})", "a read finds the instance that has the characteristic");
  r.call("a", "stop", R"({"id":2})", 0);
  int offs = 0;
  for (const auto& [link, pdu] : r.radio.sent)
    if (pdu.size() == 5 && pdu[0] == att::kWriteReq && pdu[3] == 0 && pdu[4] == 0) ++offs;
  check(offs == 2, "stopping switches both instances off");
}

// A peer that stops answering in the middle of discovery: the timeout drops the link from
// inside tick(), which walks the links (run under ASan).
void silentDuringDiscovery() {
  Rig r;
  r.radio.silent = true;
  r.call("a", "connect", R"({"addr":"C4:DE:E2:1F:D1:B6"})", 1);
  r.call("b", "connect", R"({"addr":"11:22:33:44:55:66"})", 2);
  r.now += GattClient::kRequestTimeoutMs + 1;
  r.hub.tick(r.now);
  r.settle();
  check(has(r.events.last("a", 1), "discovery failed") && r.events.of("a", 1).back().done,
        "a peer silent during discovery ends the connect with an error");
  check(has(r.events.last("b", 2), "discovery failed"), "and so does the second silent peer in the same tick");
}

// A read in flight when the link goes is answered, not left hanging.
void readInFlightWhenTheLinkDrops() {
  Rig r;
  std::vector<uint16_t> handles;
  r.radio.peerDb.addService(Uuid::from16(0x180d), {{Uuid::from16(0x2a38), att::kPropRead, false, {1}, {}}}, handles);
  r.radio.peerDb.drain();
  r.call("a", "connect", R"({"addr":"C4:DE:E2:1F:D1:B6"})", 1);
  r.radio.silent = true;
  r.call("a", "read", R"({"conn":1,"svc":"180d","chr":"2a38"})", 2);
  check(r.events.of("a", 2).empty(), "the read waits for the peer");
  r.radio.events.disconnected(r.radio.peers.begin()->first);
  check(r.events.last("a", 2) == R"({"error":"disconnected"})" && r.events.of("a", 2).back().done,
        "the lost link answers the read");
  check(has(r.events.last("a", 1), "\"disconnected\":true"), "and ends the connection");
}

void powerLossWithACentral() {
  Rig r;
  r.call("a", "serve", R"({"uuid":"fff0","chars":[{"uuid":"fff1","props":"rn"}]})", 1);
  r.radio.events.accepted(7, mkaddr("11:22:33:44:55:66", true));
  r.radio.events.power(Power::Failed, "gone");
  check(has(r.events.last("a", 1), "\"central\":\"11:22:33:44:55:66\",\"connected\":false"),
        "a central connected when the controller goes is reported gone");
  check(!r.hub.idle(), "the service itself is kept for when the controller returns");
}

void dedupeAfterManyAddresses() {
  Rig r;
  r.call("a", "scan", R"({"dedupe":5000})", 1);
  char addr[18];
  for (int i = 0; i < 400; ++i) {
    std::snprintf(addr, sizeof addr, "AA:BB:CC:DD:%02X:%02X", i / 256, i % 256);
    r.now += 100;
    r.hub.tick(r.now);
    r.radio.advert(addr, 3, -60, advWith("x", 0x1234));
  }
  const std::size_t before = r.events.of("a", 1).size();
  r.radio.advert("AA:BB:CC:DD:01:8F", 3, -60, advWith("x", 0x1234));
  check(r.events.of("a", 1).size() == before, "the newest addresses are still inside their dedupe window");
  r.now += 6000;
  r.hub.tick(r.now);
  r.radio.advert("AA:BB:CC:DD:00:00", 3, -60, advWith("x", 0x1234));
  check(r.events.of("a", 1).size() == before + 1, "an address whose window has passed reports again");
}

// A watch that connected to us by itself is read over its own link: a second connection
// would be refused (EBUSY on the device).
void reuseACentralsLink() {
  Rig r;
  std::vector<uint16_t> handles;
  r.radio.peerDb.addService(Uuid::from16(0x180d), {{Uuid::from16(0x2a37), att::kPropNotify, false, {}, {}}}, handles);
  r.radio.peerDb.drain();
  r.call("a", "serve", R"({"uuid":"fff0","chars":[{"uuid":"fff1","props":"r"}]})", 9);
  const Address watch = mkaddr("64:F8:48:E2:FF:64", true);
  r.radio.peers[7] = watch;
  r.radio.levels[7] = 1;
  r.radio.peerDb.linkUp(7);
  r.radio.events.accepted(7, watch);
  r.call("a", "connect", R"({"addr":"64:F8:48:E2:FF:64","random":true})", 1);
  check(r.radio.connectCalls == 0, "no second connection to a peer already connected to us");
  check(has(r.events.last("a", 1), "\"connected\":true"), "the script is connected over the central's link");
  r.call("a", "subscribe", R"({"conn":1,"svc":"180d","chr":"2a37"})", 2);
  r.radio.notifyPeer(handles[0], {0, 77});
  r.settle();
  check(r.events.last("a", 2) == R"({"data":"004d"})", "its notifications arrive");
  r.call("a", "disconnect", R"({"conn":1})", 0);
  check(r.radio.disconnected.empty(), "letting go of it leaves the central's own link up");
  r.call("a", "connect", R"({"addr":"64:F8:48:E2:FF:64","random":true})", 3);
  check(has(r.events.last("a", 3), "\"connected\":true") && r.radio.connectCalls == 0, "and it can be used again");
  r.radio.events.disconnected(7);
  check(has(r.events.last("a", 3), "\"disconnected\":true"), "when the central leaves, the script hears it");
  check(has(r.events.last("a", 9), "\"connected\":false"), "and so does the serving script");
}

// The kernel keeps connecting to a peer after it turned a connect down, and its link arrives at
// our listener: the script that is still connecting gets that link.
void connectTakesTheKernelsLink() {
  Rig r;
  std::vector<uint16_t> handles;
  r.radio.peerDb.addService(Uuid::from16(0x1812), {{Uuid::from16(0x2a4d), att::kPropNotify, false, {}, {}}}, handles);
  r.radio.peerDb.drain();
  r.call("x", "scan", "{}", 1);
  r.call("x", "stop", R"({"id":1})", 0);
  const Address pad = mkaddr("E4:17:D8:BC:EC:1D");
  r.hub.call("a", "connect", R"({"addr":"E4:17:D8:BC:EC:1D"})", 1, r.now);
  const int outgoing = r.radio.nextLink - 1;
  r.radio.peers[7] = pad;
  r.radio.levels[7] = 1;
  r.radio.peerDb.linkUp(7);
  r.radio.events.accepted(7, pad);
  r.settle();
  check(has(r.events.last("a", 1), "\"connected\":true") && r.events.of("a", 1).size() == 1,
        "the connecting script is connected once, over the kernel's link");
  check(std::count(r.radio.disconnected.begin(), r.radio.disconnected.end(), outgoing) >= 1 &&
            std::count(r.radio.disconnected.begin(), r.radio.disconnected.end(), 7) == 0,
        "its own attempt is dropped, the link kept");
  r.call("a", "subscribe", R"({"conn":1,"svc":"1812","chr":"2a4d"})", 2);
  r.radio.notifyPeer(handles[0], {0x01});
  r.settle();
  check(r.events.last("a", 2) == R"({"data":"01"})", "and its reports arrive");
  r.call("a", "disconnect", R"({"conn":1})", 0);
  check(std::count(r.radio.disconnected.begin(), r.radio.disconnected.end(), 7) == 1,
        "letting go closes it like a link of its own");
}

void connectFailureAndPairing() {
  Rig r;
  r.radio.failNextConnect = true;
  r.call("a", "connect", R"({"addr":"C4:DE:E2:1F:D1:B6"})", 1);
  check(has(r.events.last("a", 1), "\"error\"") && r.events.of("a", 1).back().done, "a failed connect ends with an error");
  std::vector<uint16_t> handles;
  r.radio.peerDb.addService(uuid("fff0"), {{uuid("fff1"), att::kPropWrite, false, {}, {}}}, handles);
  r.radio.peerDb.drain();
  r.radio.lockedHandle = handles[0];
  r.call("a", "connect", R"({"addr":"C4:DE:E2:1F:D1:B6"})", 2);
  r.call("a", "write", R"({"conn":2,"svc":"fff0","chr":"fff1","data":"01"})", 3);
  check(r.events.last("a", 3) == "{\"ok\":true}", "a write that needs pairing pairs and retries on its own");
  check(r.radio.levels.begin()->second >= 1 && r.radio.levels.rbegin()->second == 2, "the link ends up encrypted");
  r.call("a", "pair", R"({"conn":2})", 4);
  check(r.events.last("a", 4) == "{\"paired\":true}", "pairing an encrypted link answers at once");
}

// A central found us by an advert as often as by a service: the advertising owner hears of it
// too, with the kind of address it has.
void centralsReachAdvertisers() {
  Rig r;
  r.call("a", "advertise", R"({"name":"NG-A"})", 1);
  r.call("b", "scan", "{}", 2);
  r.radio.events.accepted(7, mkaddr("11:22:33:44:55:66", true));
  check(r.events.last("a", 1) == R"({"central":"11:22:33:44:55:66","connected":true,"random":true})",
        "the advertising owner hears a central connect");
  check(r.events.of("b", 2).empty(), "an owner that only scans does not");
  r.radio.events.disconnected(7);
  check(r.events.last("a", 1) == R"({"central":"11:22:33:44:55:66","connected":false,"random":true})" &&
            !r.events.of("a", 1).back().done,
        "and leave, its advert still running");
  const Address phone = mkaddr("AC:E4:B5:D6:D0:57");
  r.radio.events.accepted(8, phone);
  r.hub.disconnect(phone);
  check(r.radio.disconnected == std::vector<int>{8}, "the firmware can close a central's own link");
}

void advertisingSlots() {
  Rig r;
  check(r.call("a", "advertise", R"({"name":"NG-A","uuids":["180d"]})", 1) == "{\"ok\":true}", "a first advert is accepted");
  check(r.radio.adverts.size() == 1, "it reaches the controller once powered");
  check(has(r.call("a", "advertise", R"({"name":"NG-A2"})", 2), "already") ||
            has(r.call("a", "advertise", R"({"name":"NG-A2"})", 2), "too many"),
        "one advert per script");
  r.call("b", "advertise", R"({"mfg":[{"id":65535,"data":"4e47"}]})", 5);
  check(r.radio.adverts.size() == 2, "a second script gets the second slot");
  check(has(r.call("c", "advertise", R"({"name":"NG-C"})", 9), "no advertising slot"), "slots run out cleanly");
  r.hub.forget("a", r.now);
  check(r.radio.adverts.size() == 1, "forgetting a frees its slot");
  check(r.call("c", "advertise", R"({"name":"NG-C"})", 9) == "{\"ok\":true}", "and c can have it");
  check(has(r.call("d", "advertise", R"({"mfg":[{"id":1,"data":")" + std::string(80, 'a') + R"("}]})", 1), "31 bytes"),
        "an advert too big for a packet is refused");
}

void serving() {
  Rig r;
  check(r.call("a", "serve", R"({"uuid":"fff0","chars":[{"uuid":"fff1","props":"rwn","value":"00"}]})", 1) ==
            "{\"ok\":true}",
        "a service is accepted");
  check(has(r.call("b", "serve", R"({"uuid":"fff0","chars":[{"uuid":"fff1","props":"r"}]})", 2), "already served"),
        "a second script cannot serve the same uuid");
  check(r.call("b", "serve", R"({"uuid":"fee0","chars":[{"uuid":"fee1","props":"w"}]})", 2) == "{\"ok\":true}",
        "another uuid is fine");
  check(has(r.call("b", "serve", R"({"uuid":"fed0","chars":[{"uuid":"fed1","props":"q"}]})", 3), "bad property"),
        "an unknown property letter is refused");
  r.radio.events.accepted(7, mkaddr("11:22:33:44:55:66", true));
  check(has(r.events.last("a", 1), "\"central\":\"11:22:33:44:55:66\"") && has(r.events.last("b", 2), "\"connected\":true"),
        "serving scripts hear a central connect");
  GattClient phone([&](const Bytes& pdu) {
    r.radio.later([&r, pdu] { r.radio.events.att(7, pdu.data(), pdu.size()); });
    return true;
  });
  bool ready = false;
  phone.onReady = [&](bool ok) { ready = ok; };
  r.radio.peers.erase(7);
  auto route = [&] {
    for (int guard = 0; guard < 200; ++guard) {
      r.settle();
      bool any = false;
      for (auto it = r.radio.sent.begin(); it != r.radio.sent.end();) {
        if (it->first == 7) {
          phone.receive(it->second.data(), it->second.size(), r.now);
          it = r.radio.sent.erase(it);
          any = true;
        } else {
          ++it;
        }
      }
      if (!any && r.radio.queue.empty()) break;
    }
  };
  phone.start(r.now);
  route();
  check(ready && phone.find(uuid("fff0"), uuid("fff1")) && phone.find(uuid("fee0"), uuid("fee1")),
        "a central discovers every script's service");
  const GattClient::Characteristic* a = phone.find(uuid("fff0"), uuid("fff1"));
  const GattClient::Characteristic* b = phone.find(uuid("fee0"), uuid("fee1"));
  phone.write(b->value, {0x42}, true, [](bool, uint8_t, const Bytes&) {});
  route();
  check(has(r.events.last("b", 2), "\"write\":\"fee1\",\"data\":\"42\"") && !has(r.events.last("a", 1), "\"write\""),
        "a write reaches only the owning script");
  phone.write(a->clientConfig, {1, 0}, true, [](bool, uint8_t, const Bytes&) {});
  route();
  check(has(r.events.last("a", 1), "\"subscribe\":\"fff1\",\"on\":true"), "the owner hears a subscription");
  std::vector<Bytes> notes;
  phone.onValue = [&](uint16_t, const Bytes& v) { notes.push_back(v); };
  check(r.call("a", "set", R"({"svc":1,"chr":"fff1","data":"2a"})", 0) == "{\"ok\":true}", "set updates a value");
  route();
  check(notes.size() == 1 && notes[0] == Bytes({0x2a}), "and notifies the subscribed central");
  check(has(r.call("b", "set", R"({"svc":1,"chr":"fff1","data":"2a"})", 0), "not serving"),
        "a script cannot set another script's value");
  r.hub.forget("a", r.now);
  route();
  check(has(r.call("c", "serve", R"({"uuid":"fff0","chars":[{"uuid":"fff1","props":"r"}]})", 4), "ok"),
        "a's uuid is free again after forget");
}

void idlePowerOff() {
  Rig r;
  r.call("a", "scan", "{}", 1);
  r.call("a", "stop", R"({"id":1})", 0);
  r.hub.tick(r.now);
  r.now += BleHub::kIdleOffMs - 1;
  r.hub.tick(r.now);
  check(r.radio.powerCalls.size() == 1, "the controller stays on through a short pause");
  r.now += 2;
  r.hub.tick(r.now);
  r.settle();
  check(r.radio.powerCalls.size() == 2 && !r.radio.powerCalls.back(), "idle for the whole window switches it off");
  check(has(r.call("a", "state", "{}", 0), "\"off\""), "state reports off");
  r.call("a", "scan", "{}", 2);
  check(r.radio.powerCalls.size() == 3 && r.radio.scanning, "the next user switches it on again");
}

}

// The report map of an 8BitDo Ultimate 2 in Bluetooth mode, as it reads it out.
Bytes eightBitDoMap() {
  Bytes map;
  fromHex("05010905a1018501050115002507463b0195017504651409398142750195048101150026ff00093009310932"
          "09359504750881020502150026ff0009c409c5950275088102050919012918150025017501951881020600ff"
          "0920750895178102050f0970850515002564750895049102c0", map);
  return map;
}

Bytes eightBitDoReport(uint8_t hat, uint32_t buttons, uint8_t lx = 128, uint8_t rt = 0) {
  Bytes r(33, 0);
  r[0] = hat;
  r[1] = lx;
  r[2] = r[3] = r[4] = 128;
  r[5] = rt;
  r[7] = static_cast<uint8_t>(buttons);
  r[8] = static_cast<uint8_t>(buttons >> 8);
  r[9] = static_cast<uint8_t>(buttons >> 16);
  return r;
}

void hidReports() {
  HidLayout l;
  check(parseHidReportMap(eightBitDoMap(), l) && l.length == 33, "the 8BitDo report is 33 bytes");
  check(l.hat.bit == 0 && l.hat.bits == 4 && l.lx.bit == 8 && l.ly.bit == 16 && l.rx.bit == 24 && l.ry.bit == 32,
        "D-pad in the low nibble, the sticks in the next four bytes");
  check(l.rt.bit == 40 && l.lt.bit == 48 && l.buttons.bit == 56 && l.buttons.bits == 24,
        "triggers after them, then 24 buttons");
  HidControls c;
  check(readHidReport(eightBitDoReport(0x0f, 0x000801, 0, 255), l, c) && c.hat == -1 && c.buttons == 0x801 &&
            c.axes[0] == 0 && c.axes[1] == 128 && c.triggers[1] == 255 && c.triggers[0] == 0,
        "a report reads as buttons, a released D-pad, the left stick and the right trigger");
  check(readHidReport(eightBitDoReport(2, 0), l, c) && c.hat == 2, "the D-pad points right");
  check(!readHidReport(Bytes(20, 0), l, c), "a report of another length is not the gamepad's");

  // Signed 16-bit sticks and no report id, as other gamepads send them.
  Bytes map;
  fromHex("05010905a101" "0930093116008026ff7f751095028102" "05091901290815002501750195088102" "c0", map);
  HidLayout wide;
  check(parseHidReportMap(map, wide) && wide.length == 5 && !wide.hat.valid(), "a map without report ids");
  check(readHidReport(Bytes{0x00, 0x80, 0xff, 0x7f, 0x05}, wide, c) && c.axes[0] == 0 && c.axes[1] == 255 &&
            c.buttons == 5 && c.hat == -1,
        "signed 16-bit sticks scale to a byte");
  fromHex("05010906a101050719e029e71500250175019508810295067508150025650507190029658100c0", map);
  check(!parseHidReportMap(map, wide), "a keyboard is not a gamepad");
  fromHex("05010902a1010901a100050919012903150025017501950381027505950181010501093009311581257f750895028106c0c0", map);
  check(!parseHidReportMap(map, wide), "nor is a mouse, though it has buttons");
  check(l.reportId == 1, "the 8BitDo numbers its gamepad report 1");
}

// The gamepad stays connected without a script: paired once, remembered, found again.
struct PadRig {
  char directory[32] = "/tmp/awtrix-gamepad-XXXXXX";
  std::string path;
  FakeRadio radio;
  std::unique_ptr<GamepadManager> pad;
  BleHub hub{radio, [this](BleEvent e) {
               if (pad->owns(e.script)) pad->onEvent(std::move(e));
             }};
  std::vector<uint16_t> handles;
  std::vector<std::string> logs;
  int64_t now = 1000;
  PadRig() {
    check(::mkdtemp(directory) != nullptr, "gamepad fixture gets a directory");
    path = std::string(directory) + "/gamepad.json";
    // The gamepad's report 1 and a second input report of the same length, report 2.
    radio.peerDb.addService(Uuid::from16(0x1812),
                            {{Uuid::from16(0x2a4b), att::kPropRead, false, eightBitDoMap(), {}},
                             {Uuid::from16(0x2a4d), att::kPropNotify, false, {}, {{Uuid::from16(0x2908), Bytes{1, 1}}}},
                             {Uuid::from16(0x2a4d), att::kPropNotify, false, {}, {{Uuid::from16(0x2908), Bytes{2, 1}}}}},
                            handles);
    radio.peerDb.drain();
    fresh();
  }
  ~PadRig() {
    ::unlink(path.c_str());
    ::rmdir(directory);
  }
  void fresh() {
    pad = std::make_unique<GamepadManager>(hub, path, [this](const std::string& line) { logs.push_back(line); });
  }
  void step() {
    for (int i = 0; i < 30; ++i) {
      radio.pump(hub, now);
      pad->tick(now);
    }
  }
  GamepadStatus status() const { return pad->input(1).state; }
  uint32_t buttons(int player = 1) const { return pad->input(player).controls.buttons; }
  void advert(const char* addr, const char* name = "8BitDo Ultimate") {
    radio.advert(addr, 0, -50, advWith(name, 0x1812));
    step();
  }
  void report(uint32_t buttons, int instance = 0) {
    radio.notifyPeer(handles[1 + instance], eightBitDoReport(15, buttons));
    step();
  }
  int linkOf(const char* address) const {
    for (auto it = radio.peers.rbegin(); it != radio.peers.rend(); ++it)
      if (it->second.str() == address) return it->first;
    return -1;
  }
  void reportTo(const char* address, uint32_t buttons, int hat = 15) {
    Bytes pdu{att::kNotify};
    put16(pdu, handles[1]);
    const Bytes value = eightBitDoReport(hat, buttons);
    pdu.insert(pdu.end(), value.begin(), value.end());
    radio.events.att(linkOf(address), pdu.data(), pdu.size());
    step();
  }
};

void builtInGamepad() {
  PadRig r;
  r.pad->start(r.now);
  r.step();
  check(r.status() == GamepadStatus::Unpaired && r.radio.powerCalls.empty(), "with none paired Bluetooth stays off");

  r.pad->pair(r.now);
  r.step();
  check(r.status() == GamepadStatus::Pairing && r.radio.scanning && r.radio.scanActive, "pairing searches actively");
  r.radio.advert("AA:BB:CC:DD:EE:01", 0, -50, advWith("Heart", 0x180d));
  r.advert("E4:17:D8:BC:EC:1D");
  check(r.status() == GamepadStatus::Connecting && r.radio.connectCalls == 1, "the first HID device is connected");
  r.report(0x1);
  check(r.status() == GamepadStatus::Ready && r.pad->name(1) == "8BitDo Ultimate" &&
            r.pad->devices()[0].address == "E4:17:D8:BC:EC:1D" && r.buttons() == 1,
        "its first report makes it ready, named, with its controls");
  r.report(0x7, 1);
  check(r.buttons() == 1, "a report of the same length from another report id is not the gamepad's");
  std::string stored;
  check(posix::readText(r.path, stored) && stored.find("E4:17:D8:BC:EC:1D") != std::string::npos, "it is remembered");

  r.radio.events.disconnected(r.radio.peers.rbegin()->first);
  r.step();
  check(r.status() == GamepadStatus::Waiting && r.buttons() == 0, "a lost gamepad is waited for, released");
  r.now += Gamepad::kRetryMs;
  r.step();
  check(r.radio.scanning && !r.radio.scanActive && r.radio.scanBackground, "by a passive background scan for its address");
  r.advert("AA:BB:CC:DD:EE:02", "Other pad");
  check(r.status() == GamepadStatus::Waiting && r.radio.connectCalls == 1, "another gamepad is not taken");
  r.advert("E4:17:D8:BC:EC:1D");
  r.report(0x2);
  check(r.status() == GamepadStatus::Ready && r.buttons() == 2 && r.radio.connectCalls == 2,
        "its own adverts bring it back");

  // Restarted: the remembered gamepad is known at once, and a link the system made is taken over.
  r.fresh();
  check(r.status() == GamepadStatus::Waiting && r.pad->name(1) == "8BitDo Ultimate", "the remembered gamepad is known before start");
  r.now += 5000;
  r.pad->start(r.now);
  r.step();
  const int calls = r.radio.connectCalls;
  r.radio.peers[9] = mkaddr("E4:17:D8:BC:EC:1D");
  r.radio.levels[9] = 1;
  r.radio.peerDb.linkUp(9);
  r.radio.events.accepted(9, mkaddr("E4:17:D8:BC:EC:1D"));
  r.step();
  r.report(0x4);
  check(r.status() == GamepadStatus::Ready && r.buttons() == 4 && r.radio.connectCalls == calls,
        "a link the system made to it is taken over at once");

  // A report map that does not read as a gamepad once does not lose the remembered one.
  r.radio.events.disconnected(9);
  r.step();
  r.radio.peerDb.setValue(r.handles[0], Bytes{0x05, 0x01});
  r.now += Gamepad::kRetryMs;
  r.step();
  r.advert("E4:17:D8:BC:EC:1D");
  check(r.status() != GamepadStatus::Ready, "a gamepad whose report map is unreadable is not ready");
  r.radio.peerDb.setValue(r.handles[0], eightBitDoMap());
  r.now += Gamepad::kRetryMaxMs;
  r.step();
  r.advert("E4:17:D8:BC:EC:1D");
  r.report(0x8);
  check(r.status() == GamepadStatus::Ready && r.buttons() == 8, "but is taken again once it reads");

  r.pad->forget(1, r.now);
  r.step();
  check(r.status() == GamepadStatus::Unpaired && posix::readText(r.path, stored) && stored.find("\"devices\":[]") != std::string::npos &&
            r.radio.forgotten == std::vector<std::string>{"E4:17:D8:BC:EC:1D"},
        "forgetting it lets it go, bond included");
}

void pairingEnds() {
  PadRig r;
  r.pad->pair(r.now);
  r.step();
  r.now += Gamepad::kPairingMs;
  r.step();
  check(r.status() == GamepadStatus::Unpaired && !r.radio.scanning, "a minute without a gamepad ends pairing");
}

void retriesSlowDown() {
  PadRig r;
  r.pad->pair(r.now);
  r.step();
  r.advert("E4:17:D8:BC:EC:1D");
  r.report(0x1);
  r.radio.events.disconnected(r.radio.peers.rbegin()->first);
  r.step();
  r.now += Gamepad::kRetryMs;
  r.step();
  r.radio.failNextConnect = true;
  r.advert("E4:17:D8:BC:EC:1D");
  int scans = r.radio.scanCalls;
  r.now += 2 * Gamepad::kRetryMs - 1;
  r.step();
  check(r.radio.scanCalls == scans, "a failed connect after a lost link is the second failure: it waits 4 s");
  r.now += 1;
  r.step();
  check(r.radio.scanCalls > scans, "and then looks again");
  r.radio.failNextConnect = true;
  r.advert("E4:17:D8:BC:EC:1D");
  scans = r.radio.scanCalls;
  r.now += 4 * Gamepad::kRetryMs - 1;
  r.step();
  check(r.radio.scanCalls == scans, "the third failure in a row waits twice as long again");
  r.now += 1;
  r.step();
  check(r.radio.scanCalls > scans, "and then looks again");
}

void twoGamepadsKeepIndependentPlayers() {
  const char* first = "E4:17:D8:BC:EC:1D";
  const char* second = "E4:17:D8:BC:EC:2E";
  PadRig r;
  check(r.pad->pair(r.now).id == 1, "the first device slot is paired");
  r.step();
  r.advert(first, "First");
  r.reportTo(first, 1, 1);
  check(r.pad->pair(r.now).id == 2, "adding a pad selects the second device slot");
  r.step();
  const int calls = r.radio.connectCalls;
  r.advert(first);
  check(r.radio.connectCalls == calls, "the second pairing ignores the already known controller");
  r.advert(second, "Second");
  r.reportTo(second, 2, 5);
  check(r.buttons(1) == 1 && r.buttons(2) == 2 && r.pad->input(1).controls.hat != r.pad->input(2).controls.hat &&
            r.pad->name(2) == "Second",
        "two simultaneous controllers have independent reports");
  check(r.pad->pair(r.now).status == GamepadPairStatus::Full, "a third paired device is refused");
  r.radio.disconnect(r.linkOf(first));
  r.step();
  check(r.pad->devices()[0].player == 0 && r.pad->devices()[1].player == 1 && r.buttons(1) == 2 &&
            r.pad->input(2).state == GamepadStatus::Waiting && r.buttons(2) == 0 && r.pad->name(2) == "First",
        "the one controller left is Player 1; Player 2 shows the one that is away, released");
  r.now += Gamepad::kRetryMs;
  r.step();
  r.advert(first, "First");
  r.reportTo(first, 4);
  check(r.pad->devices()[0].player == 2 && r.buttons(1) == 2 && r.buttons(2) == 4,
        "the controller that comes back is Player 2 and Player 1 keeps its input");
  r.pad->forget(1, r.now);
  r.step();
  check(r.pad->devices()[0].state == GamepadStatus::Unpaired && r.buttons(1) == 2 &&
        r.radio.forgotten == std::vector<std::string>{first}, "forget removes only the selected device and bond");
}

void reportArrivalOrderSurvivesRestart() {
  const char* first = "E4:17:D8:BC:EC:1D";
  const char* second = "E4:17:D8:BC:EC:2E";
  PadRig r;
  r.pad->pair(r.now);
  r.step(); r.advert(first); r.reportTo(first, 1);
  r.pad->pair(r.now);
  r.step(); r.advert(second); r.reportTo(second, 2);
  r.radio.disconnect(r.linkOf(first));
  r.radio.disconnect(r.linkOf(second));
  r.step();
  r.fresh();
  r.pad->start(r.now);
  r.step();
  r.advert(first); r.advert(second);
  // Both reports arrive before the manager's next tick, in reverse registry order.
  auto notify = [&](const char* address, uint32_t buttons) {
    Bytes pdu{att::kNotify}; put16(pdu, r.handles[1]);
    const auto report = eightBitDoReport(15, buttons);
    pdu.insert(pdu.end(), report.begin(), report.end());
    r.radio.events.att(r.linkOf(address), pdu.data(), pdu.size());
  };
  notify(second, 8);
  notify(first, 16);
  r.step();
  check(r.pad->devices()[1].player == 1 && r.pad->devices()[0].player == 2 &&
        r.buttons(1) == 8 && r.buttons(2) == 16,
        "report arrival order determines players, even when both become ready in one tick");
}

void registryMigrationAndValidation() {
  PadRig r;
  const std::string legacy = R"({"addr":"E4:17:D8:BC:EC:1D","random":false,"name":"Legacy"})";
  check(posix::replaceText(r.path, legacy), "legacy registry fixture is written");
  r.fresh();
  std::string stored;
  check(r.pad->devices()[0].name == "Legacy" && posix::readText(r.path, stored) && stored == legacy,
        "the old registry loads without rewriting it");
  r.pad->start(r.now); r.step();
  r.pad->pair(r.now); r.step();
  r.advert("E4:17:D8:BC:EC:2E", "New"); r.reportTo("E4:17:D8:BC:EC:2E", 2);
  check(posix::readText(r.path, stored) && stored.find("\"version\":2") != std::string::npos &&
        stored.find("Legacy") != std::string::npos && stored.find("New") != std::string::npos,
        "the next registry mutation saves both devices in the versioned format");
  for (const std::string bad : {R"({"version":3,"devices":[]})",
       R"({"version":2,"devices":[{"id":3,"addr":"E4:17:D8:BC:EC:1D","name":"Bad"}]})",
       R"({"version":2,"devices":[{"id":1,"addr":"E4:17:D8:BC:EC:1D","name":"A"},{"id":2,"addr":"E4:17:D8:BC:EC:1D","name":"B"}]})"}) {
    posix::replaceText(r.path, bad); r.fresh();
    check(r.pad->devices()[0].state == GamepadStatus::Unpaired &&
          r.pad->devices()[1].state == GamepadStatus::Unpaired, "invalid registry is rejected as a whole");
  }
  posix::replaceText(r.path, R"({"addr":"E4:17:D8:BC:EC:1D"})"); r.fresh();
  check(r.pad->devices()[0].state == GamepadStatus::Waiting,
        "legacy files without optional name or address type still load");
}

void failedSaveWaitsForTheNextChange() {
  PadRig r;
  r.path = std::string(r.directory) + "/later/gamepad.json";
  r.fresh();
  r.pad->pair(r.now); r.step(); r.advert("E4:17:D8:BC:EC:1D"); r.report(1);
  check(r.status() == GamepadStatus::Ready, "a storage failure does not discard live controls");
  const std::string parent = std::string(r.directory) + "/later";
  check(::mkdir(parent.c_str(), 0700) == 0, "storage becomes writable again");
  r.now += Gamepad::kRetryMaxMs; r.step();
  std::string stored;
  const auto failures = std::count_if(r.logs.begin(), r.logs.end(),
                                      [](const std::string& line) { return line.find("cannot store") != std::string::npos; });
  check(!posix::readText(r.path, stored) && failures == 1, "a failed save is logged once and not retried on a timer");
  r.pad->pair(r.now); r.step(); r.advert("E4:17:D8:BC:EC:2E", "Second"); r.reportTo("E4:17:D8:BC:EC:2E", 2);
  check(posix::readText(r.path, stored) && stored.find("E4:17:D8:BC:EC:1D") != std::string::npos &&
            stored.find("E4:17:D8:BC:EC:2E") != std::string::npos,
        "the next change stores the whole registry");
  ::unlink(r.path.c_str()); ::rmdir(parent.c_str());
}

void pairingIsBoundedAndIdempotent() {
  PadRig r;
  r.pad->pair(r.now); r.step(); r.advert("E4:17:D8:BC:EC:1D"); r.report(1);
  check(r.pad->pair(r.now).id == 2, "second device starts pairing"); r.step();
  const int scans = r.radio.scanCalls;
  r.now += Gamepad::kPairingMs - 1;
  check(r.pad->pair(r.now).id == 2 && r.radio.scanCalls == scans, "a repeated pair does not restart its search");
  ++r.now; r.step();
  check(r.pad->devices()[1].state == GamepadStatus::Unpaired && r.buttons(1) == 1,
        "pairing timeout preserves the first controller");
}

int main() {
  uint8_t mac[6] = {};
  check(posix::parseMac("01:23:45:67:89:aB", mac) && mac[0] == 1 && mac[5] == 0xab, "MAC network byte order");
  check(posix::parseMac("01:23:45:67:89:aB", mac, true) && mac[0] == 0xab && mac[5] == 1, "MAC controller byte order");
  check(!posix::parseMac("01:23:45:67:89:aZ", mac) && mac[0] == 0xab, "invalid MAC leaves output intact");
  check(!posix::parseMac("1:23:45:67:89:ab", mac) && !posix::parseMac("01:23:45:67:89:abjunk", mac),
        "MAC rejects short pairs and suffixes");
  Bytes binary{42};
  check(!posix::fromHex("01x2", binary) && binary == Bytes{42}, "invalid hex leaves output intact");
  check(posix::fromHex("01Abff", binary) && binary == Bytes({1, 0xab, 0xff}), "mixed-case hex");
  binary.clear();
  posix::put16(binary, 0x1234); posix::put32(binary, 0xfedcba98);
  check(binary == Bytes({0x34, 0x12, 0x98, 0xba, 0xdc, 0xfe}) &&
        posix::le16(binary.data()) == 0x1234 && posix::le32(binary.data() + 2) == 0xfedcba98, "unaligned little-endian codecs");

  twoGamepadsKeepIndependentPlayers();
  reportArrivalOrderSurvivesRestart();
  registryMigrationAndValidation();
  failedSaveWaitsForTheNextChange();
  pairingIsBoundedAndIdempotent();
  types();
  advertising();
  clientAgainstServer();
  encryptedSubscriptions();
  persistentBonds();
  sharedScan();
  rateLimit();
  sharedConnection();
  duplicateServices();
  thinnedSubscription();
  subscribePairsWhenAsked();
  reuseACentralsLink();
  connectTakesTheKernelsLink();
  silentDuringDiscovery();
  readInFlightWhenTheLinkDrops();
  powerLossWithACentral();
  dedupeAfterManyAddresses();
  connectFailureAndPairing();
  centralsReachAdvertisers();
  advertisingSlots();
  serving();
  idlePowerOff();
  hidReports();
  builtInGamepad();
  pairingEnds();
  retriesSlowDown();
  if (failures) std::printf("%d failure(s)\n", failures);
  else std::printf("ble: all checks passed\n");
  return failures ? 1 : 0;
}
