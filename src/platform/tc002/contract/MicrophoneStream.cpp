#include "platform/tc002/contract/MicrophoneStream.h"

#include <climits>

#include "core/api/JsonReader.h"
#include "core/api/JsonWriter.h"
#include "platform/tc002/contract/Pcm16.h"

namespace awtrix::tc002 {
namespace {
bool number(const api::JsonReader& r, const char* key, long long max,
            long long& n) {
  const auto f = api::memberValue(r, key);
  return f.isInteger() && f.asLong(n) && n >= 0 && n <= max;
}
bool valid(const StreamEvent& e) {
  if (e.epoch <= 0 || e.hostAtMs < 0 || e.error.size() > 120 ||
      e.error.find('\0') != std::string::npos)
    return false;
  switch (e.kind) {
    case StreamEvent::Kind::Started:
      return e.samples.empty() && e.error.empty() && e.firstSample == 0;
    case StreamEvent::Kind::Audio:
      return e.error.empty() && !e.samples.empty() &&
             e.samples.size() <= kStreamBlockSamples &&
             e.samples.size() % 24 == 0 && e.firstSample % 24 == 0;
    case StreamEvent::Kind::Ended:
      return e.samples.empty() && e.firstSample % 24 == 0;
    case StreamEvent::Kind::Support:
      return e.samples.empty() && e.error.empty() && e.firstSample == 0;
  }
  return false;
}
}  // namespace
std::string encodeStreamControl(const StreamControl& c) {
  if (c.epoch <= 0 || c.operation < StreamControl::Probe ||
      c.operation > StreamControl::Keepalive)
    return {};
  std::string out;
  api::JsonWriter(out)
      .beginObject()
      .member("v", 2)
      .member("type", "microphoneStreamControl")
      .member("epoch", c.epoch)
      .member("operation", static_cast<int>(c.operation))
      .endObject();
  return out;
}
std::string encodeStreamEvent(const StreamEvent& e) {
  if (!valid(e)) return {};
  std::string out;
  api::JsonWriter(out)
      .beginObject()
      .member("v", 2)
      .member("type", "microphoneStreamEvent")
      .member("epoch", e.epoch)
      .member("kind", static_cast<int>(e.kind))
      .member("firstSample", e.firstSample)
      .member("capturedMs", e.capturedMs)
      .member("hostAtMs", static_cast<long long>(e.hostAtMs))
      .member("available", e.available)
      .member("error", e.error)
      .member("pcm", encodePcm16(e.samples))
      .endObject();
  return out;
}
bool decodeStreamControl(std::string_view json, StreamControl& out) {
  api::JsonReader r(json);
  long long epoch = 0, operation = 0;
  if (!number(r, "epoch", INT_MAX, epoch) || !epoch ||
      !number(r, "operation", 3, operation))
    return false;
  out = {static_cast<int>(epoch),
         static_cast<StreamControl::Operation>(operation)};
  return true;
}
bool decodeStreamEvent(std::string_view json, StreamEvent& out) {
  api::JsonReader r(json);
  StreamEvent e;
  long long epoch = 0, kind = 0, position = 0, tick = 0, host = 0;
  std::string encoded;
  if (!number(r, "epoch", INT_MAX, epoch) || !epoch ||
      !number(r, "kind", 3, kind) ||
      !number(r, "firstSample", UINT32_MAX, position) ||
      !number(r, "capturedMs", UINT32_MAX, tick) ||
      !number(r, "hostAtMs", INT64_MAX, host) ||
      !api::memberValue(r, "available").asBool(e.available) ||
      !api::memberValue(r, "error").appendString(e.error) ||
      !api::memberValue(r, "pcm").appendString(encoded) ||
      encoded.size() > kStreamBlockSamples * 8 / 3)
    return false;
  e.epoch = static_cast<int>(epoch);
  e.kind = static_cast<StreamEvent::Kind>(kind);
  e.firstSample = static_cast<uint32_t>(position);
  e.capturedMs = static_cast<uint32_t>(tick);
  e.hostAtMs = host;
  if (!decodePcm16(encoded, e.samples) || e.samples.size() % 24) return false;
  if (!valid(e)) return false;
  out = std::move(e);
  return true;
}
}  // namespace awtrix::tc002
