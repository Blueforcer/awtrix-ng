#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>

#include "core/net/Datagram.h"
#include "core/render/Canvas.h"

namespace awtrix {
namespace remote {
namespace wire {

// The link between an ESP32 clock and the script host that runs its scripts. Every datagram starts
// with "AWRH", the version and the type; integers are little endian. The clock sends Hello every
// two seconds and Show whenever the app on its display, the app sliding in or a button changes; the
// host answers with Apps and with Frame datagrams for the apps Show names. Offer goes from the host
// to the clock's discovery port: a plain one advertises the host, a signed one tells a paired clock
// where its host is now.
constexpr uint8_t kVersion = 1;
constexpr std::size_t kMaxDatagram = net::kMaxDatagram;
constexpr std::size_t kMaxName = 32;
constexpr std::size_t kMaxUid = 32;
constexpr std::size_t kTagBytes = 16;
constexpr std::size_t kMaxApps = 32;
constexpr int kButtons = 3;

enum class Type : uint8_t { Hello = 1, Show = 2, Apps = 3, Frame = 4, Offer = 6 };

namespace sensor {
constexpr uint8_t kTemperature = 1u << 0;
constexpr uint8_t kHumidity = 1u << 1;
constexpr uint8_t kPressure = 1u << 2;
constexpr uint8_t kLight = 1u << 3;
constexpr uint8_t kBattery = 1u << 4;
}

namespace state {
constexpr uint8_t kDisplayOn = 1u << 0;
constexpr uint8_t kAudioPlaying = 1u << 1;
constexpr uint8_t kRotationHeld = 1u << 2;
}

namespace app {
constexpr uint8_t kTakesButtons = 1u << 0;
constexpr uint8_t kWantsShow = 1u << 1;
constexpr uint8_t kScrollHolds = 1u << 2;
constexpr uint8_t kHeadless = 1u << 3;
constexpr uint8_t kOnDemand = 1u << 4;
}

struct Sensors {
  uint8_t present = 0;
  int16_t temperatureCenti = 0;
  uint16_t humidityDeci = 0;
  uint16_t pressureDeci = 0;
  uint16_t light = 0;
  uint8_t batteryPercent = 0;
  uint16_t batteryMillivolts = 0;
};

struct Hello {
  std::string_view uid;
  uint16_t width = 0;
  uint16_t height = 0;
  uint32_t address = 0;
  uint16_t webPort = 0;
  uint32_t counter = 0;
  uint16_t settingsRevision = 0;
  uint16_t systemRevision = 0;
  uint16_t assetsRevision = 0;
  uint16_t appsRevision = 0;
  uint32_t offMask = 0;
  uint8_t state = 0;
  Sensors sensors;
};

struct Show {
  uint16_t sequence = 0;
  uint8_t buttonsDown = 0;
  uint8_t presses[kButtons] = {0, 0, 0};
  std::string_view current;
  std::string_view incoming;
};

struct AppEntry {
  std::string_view name;
  uint8_t flags = 0;
  uint32_t durationMs = 0;
};

struct Apps {
  uint16_t revision = 0;
  uint8_t count = 0;
  AppEntry entries[kMaxApps];
};

struct Frame {
  std::string_view app;
  uint16_t stampMs = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  uint16_t firstRow = 0;
  uint16_t rows = 0;
  const uint8_t* rgb = nullptr;
};

struct Offer {
  std::string_view name;
  uint16_t port = 0;
  std::string_view uid;
  uint32_t unixTime = 0;
};

struct Packet {
  Type type{};
  Hello hello;
  // For Hello and a signed Offer: the bytes the tag covers and the tag that follows them.
  std::size_t signedBytes = 0;
  const uint8_t* tag = nullptr;
  Show show;
  Apps apps;
  Frame frame;
  Offer offer;
};

// Views in the result point into data. Anything malformed or of another version is refused.
bool decode(const uint8_t* data, std::size_t length, Packet& out);

// Computes the 16-byte tag over the given bytes, an HMAC-SHA256 with the pairing key cut short.
using Signer = std::function<void(const uint8_t* data, std::size_t length, uint8_t* tag)>;

std::size_t encodeHello(const Hello& hello, const Signer& sign, uint8_t* out, std::size_t capacity);
std::size_t encodeShow(const Show& show, uint8_t* out, std::size_t capacity);
std::size_t encodeApps(const Apps& apps, uint8_t* out, std::size_t capacity);
std::size_t encodeOffer(const Offer& offer, uint8_t* out, std::size_t capacity, const Signer& sign = nullptr);

// How many rows of a frame of this app fit in one datagram.
int rowsPerDatagram(int width, std::string_view app);
// stampMs: the low 16 bits of the host's clock in milliseconds when it drew the frame.
std::size_t encodeFrame(const Canvas& canvas, std::string_view app, uint16_t stampMs, int firstRow, int rows,
                        uint8_t* out, std::size_t capacity);

}
}
}
