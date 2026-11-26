#include "core/remote/RemoteWire.h"

#include <cstring>

#include "core/net/RgbRows.h"

namespace awtrix {
namespace remote {
namespace wire {

namespace {
constexpr uint8_t kMagic[4] = {'A', 'W', 'R', 'H'};
constexpr std::size_t kHeaderBytes = 6;
constexpr std::size_t kFrameFixedBytes = 2 + 2 + 2 + 2 + 2;

struct Writer {
  uint8_t* at;
  uint8_t* end;
  bool ok = true;

  void need(std::size_t n) { ok = ok && static_cast<std::size_t>(end - at) >= n; }
  void u8(uint8_t v) {
    need(1);
    if (ok) *at++ = v;
  }
  void u16(uint16_t v) {
    u8(static_cast<uint8_t>(v & 0xFF));
    u8(static_cast<uint8_t>(v >> 8));
  }
  void u32(uint32_t v) {
    u16(static_cast<uint16_t>(v & 0xFFFF));
    u16(static_cast<uint16_t>(v >> 16));
  }
  void name(std::string_view text, std::size_t max) {
    if (text.size() > max) ok = false;
    u8(static_cast<uint8_t>(text.size()));
    need(text.size());
    if (!ok) return;
    std::memcpy(at, text.data(), text.size());
    at += text.size();
  }
};

struct Reader {
  const uint8_t* at;
  const uint8_t* end;
  bool ok = true;

  void need(std::size_t n) { ok = ok && static_cast<std::size_t>(end - at) >= n; }
  uint8_t u8() {
    need(1);
    return ok ? *at++ : 0;
  }
  uint16_t u16() {
    const uint16_t low = u8();
    return static_cast<uint16_t>(low | (u8() << 8));
  }
  uint32_t u32() {
    const uint32_t low = u16();
    return low | (static_cast<uint32_t>(u16()) << 16);
  }
  std::string_view name(std::size_t max) {
    const std::size_t length = u8();
    if (length > max) ok = false;
    need(length);
    if (!ok) return {};
    std::string_view text(reinterpret_cast<const char*>(at), length);
    at += length;
    return text;
  }
  bool done() const { return ok && at == end; }
};

Writer start(Type type, uint8_t* out, std::size_t capacity) {
  Writer w{out, out + capacity};
  w.need(kHeaderBytes);
  if (!w.ok) return w;
  std::memcpy(w.at, kMagic, sizeof(kMagic));
  w.at[4] = kVersion;
  w.at[5] = static_cast<uint8_t>(type);
  w.at += kHeaderBytes;
  return w;
}

std::size_t finish(const Writer& w, const uint8_t* out) {
  return w.ok ? static_cast<std::size_t>(w.at - out) : 0;
}

std::size_t sealed(Writer& w, uint8_t* out, const Signer& sign) {
  w.need(kTagBytes);
  if (!w.ok) return 0;
  const std::size_t body = static_cast<std::size_t>(w.at - out);
  sign(out, body, w.at);
  return body + kTagBytes;
}

bool takeTag(const Reader& r, const uint8_t* data, Packet& out) {
  if (!r.ok || static_cast<std::size_t>(r.end - r.at) != kTagBytes) return false;
  out.signedBytes = static_cast<std::size_t>(r.at - data);
  out.tag = r.at;
  return true;
}

bool decodeHello(Reader& r, const uint8_t* data, Packet& out) {
  Hello& h = out.hello;
  h.uid = r.name(kMaxUid);
  h.width = r.u16();
  h.height = r.u16();
  h.address = r.u32();
  h.webPort = r.u16();
  h.counter = r.u32();
  h.settingsRevision = r.u16();
  h.systemRevision = r.u16();
  h.assetsRevision = r.u16();
  h.appsRevision = r.u16();
  h.offMask = r.u32();
  h.state = r.u8();
  h.sensors.present = r.u8();
  h.sensors.temperatureCenti = static_cast<int16_t>(r.u16());
  h.sensors.humidityDeci = r.u16();
  h.sensors.pressureDeci = r.u16();
  h.sensors.light = r.u16();
  h.sensors.batteryPercent = r.u8();
  h.sensors.batteryMillivolts = r.u16();
  return takeTag(r, data, out) && !h.uid.empty() && h.width > 0 && h.height > 0;
}

bool decodeShow(Reader& r, Packet& out) {
  Show& s = out.show;
  s.sequence = r.u16();
  s.buttonsDown = r.u8();
  for (int i = 0; i < kButtons; ++i) s.presses[i] = r.u8();
  s.current = r.name(kMaxName);
  s.incoming = r.name(kMaxName);
  return r.done() && (s.buttonsDown >> kButtons) == 0;
}

bool decodeApps(Reader& r, Packet& out) {
  Apps& a = out.apps;
  a.revision = r.u16();
  a.count = r.u8();
  if (a.count > kMaxApps) return false;
  for (uint8_t i = 0; i < a.count; ++i) {
    AppEntry& e = a.entries[i];
    e.name = r.name(kMaxName);
    e.flags = r.u8();
    e.durationMs = r.u32();
    if (!r.ok || e.name.empty()) return false;
  }
  return r.done();
}

bool decodeFrame(Reader& r, Packet& out) {
  Frame& f = out.frame;
  f.stampMs = r.u16();
  f.width = r.u16();
  f.height = r.u16();
  f.firstRow = r.u16();
  f.rows = r.u16();
  f.app = r.name(kMaxName);
  if (!r.ok || f.app.empty() ||
      !net::rgb::validRows(f.width, f.height, f.firstRow, f.rows, static_cast<std::size_t>(r.end - r.at)))
    return false;
  f.rgb = r.at;
  return true;
}

bool decodeOffer(Reader& r, const uint8_t* data, Packet& out) {
  Offer& o = out.offer;
  o.port = r.u16();
  o.name = r.name(kMaxName);
  if (!r.ok || o.port == 0 || o.name.empty()) return false;
  if (r.done()) return true;
  o.uid = r.name(kMaxUid);
  o.unixTime = r.u32();
  return takeTag(r, data, out) && !o.uid.empty();
}
}

bool decode(const uint8_t* data, std::size_t length, Packet& out) {
  if (!data || length < kHeaderBytes || length > kMaxDatagram ||
      std::memcmp(data, kMagic, sizeof(kMagic)) != 0 || data[4] != kVersion)
    return false;
  out = Packet{};
  Reader r{data + kHeaderBytes, data + length};
  out.type = static_cast<Type>(data[5]);
  switch (out.type) {
    case Type::Hello: return decodeHello(r, data, out);
    case Type::Show: return decodeShow(r, out);
    case Type::Apps: return decodeApps(r, out);
    case Type::Frame: return decodeFrame(r, out);
    case Type::Offer: return decodeOffer(r, data, out);
  }
  return false;
}

std::size_t encodeHello(const Hello& hello, const Signer& sign, uint8_t* out, std::size_t capacity) {
  if (!sign || hello.uid.empty()) return 0;
  Writer w = start(Type::Hello, out, capacity);
  w.name(hello.uid, kMaxUid);
  w.u16(hello.width);
  w.u16(hello.height);
  w.u32(hello.address);
  w.u16(hello.webPort);
  w.u32(hello.counter);
  w.u16(hello.settingsRevision);
  w.u16(hello.systemRevision);
  w.u16(hello.assetsRevision);
  w.u16(hello.appsRevision);
  w.u32(hello.offMask);
  w.u8(hello.state);
  w.u8(hello.sensors.present);
  w.u16(static_cast<uint16_t>(hello.sensors.temperatureCenti));
  w.u16(hello.sensors.humidityDeci);
  w.u16(hello.sensors.pressureDeci);
  w.u16(hello.sensors.light);
  w.u8(hello.sensors.batteryPercent);
  w.u16(hello.sensors.batteryMillivolts);
  return sealed(w, out, sign);
}

std::size_t encodeShow(const Show& show, uint8_t* out, std::size_t capacity) {
  Writer w = start(Type::Show, out, capacity);
  w.u16(show.sequence);
  w.u8(show.buttonsDown);
  for (int i = 0; i < kButtons; ++i) w.u8(show.presses[i]);
  w.name(show.current, kMaxName);
  w.name(show.incoming, kMaxName);
  return finish(w, out);
}

std::size_t encodeApps(const Apps& apps, uint8_t* out, std::size_t capacity) {
  if (apps.count > kMaxApps) return 0;
  Writer w = start(Type::Apps, out, capacity);
  w.u16(apps.revision);
  w.u8(apps.count);
  for (uint8_t i = 0; i < apps.count; ++i) {
    if (apps.entries[i].name.empty()) return 0;
    w.name(apps.entries[i].name, kMaxName);
    w.u8(apps.entries[i].flags);
    w.u32(apps.entries[i].durationMs);
  }
  return finish(w, out);
}

std::size_t encodeOffer(const Offer& offer, uint8_t* out, std::size_t capacity, const Signer& sign) {
  if (offer.port == 0 || offer.name.empty() || (!offer.uid.empty() && !sign)) return 0;
  Writer w = start(Type::Offer, out, capacity);
  w.u16(offer.port);
  w.name(offer.name, kMaxName);
  if (offer.uid.empty()) return finish(w, out);
  w.name(offer.uid, kMaxUid);
  w.u32(offer.unixTime);
  return sealed(w, out, sign);
}

int rowsPerDatagram(int width, std::string_view app) {
  if (app.empty() || app.size() > kMaxName) return 0;
  return net::rgb::rowsPerDatagram(width, kHeaderBytes + kFrameFixedBytes + 1 + app.size());
}

std::size_t encodeFrame(const Canvas& canvas, std::string_view app, uint16_t stampMs, int firstRow, int rows,
                        uint8_t* out, std::size_t capacity) {
  const int width = canvas.width();
  if (rows <= 0 || firstRow < 0 || firstRow + rows > canvas.height() || width <= 0 || app.empty())
    return 0;
  Writer w = start(Type::Frame, out, capacity > kMaxDatagram ? kMaxDatagram : capacity);
  w.u16(stampMs);
  w.u16(static_cast<uint16_t>(width));
  w.u16(static_cast<uint16_t>(canvas.height()));
  w.u16(static_cast<uint16_t>(firstRow));
  w.u16(static_cast<uint16_t>(rows));
  w.name(app, kMaxName);
  w.need(static_cast<std::size_t>(width) * rows * 3u);
  if (!w.ok) return 0;
  w.at = net::rgb::pack(canvas, firstRow, rows, w.at);
  return finish(w, out);
}

}
}
}
