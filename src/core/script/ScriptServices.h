#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "core/render/Font.h"
#include "core/script/HttpHeaders.h"

namespace awtrix {
class Canvas;
class EffectRegistry;
struct RuntimeState;
struct Settings;
}

namespace awtrix::script {

class SharedState;

constexpr std::size_t kMaxSourceCeilingBytes = 32 * 1024;
constexpr std::size_t kDefaultMaxSourceBytes = 8 * 1024;
constexpr std::size_t kMinMaxSourceBytes = 1024;

std::size_t maxSourceBytes();
void setMaxSourceBytes(std::size_t bytes);
constexpr std::size_t kMaxHttpBody = 8 * 1024;
constexpr std::size_t kMaxHttpRequestBody = 2 * 1024;
constexpr std::size_t kMaxHttpHeaders = 8;
constexpr std::size_t kMaxHttpHeaderBytes = 256;
constexpr std::size_t kMaxStoreBytes = 2 * 1024;
constexpr std::size_t kMaxPendingHttp = 8;


// Free heap an install needs beyond the source itself. Replacing costs less because the old
// copy's memory comes back during the swap.
constexpr std::size_t kInstallHeadroomBytes = 8 * 1024;
constexpr std::size_t kReplaceHeadroomBytes = 4 * 1024;
inline std::size_t installNeedsBytes(std::size_t sourceBytes, bool replacement = false) {
  return replacement ? kReplaceHeadroomBytes + sourceBytes
                     : kInstallHeadroomBytes + sourceBytes;
}

constexpr std::size_t kInstallReserveBytes = 24 * 1024;

constexpr std::size_t kTlsWorkingSetBytes = 40 * 1024;
constexpr std::size_t kTlsContiguousBytes = 16 * 1024;

// A TLS handshake needs both a working set and one large contiguous block for the record
// buffers, and failing it halfway wedges the socket -- so the check happens before dialling.
inline bool fetchFits(bool https, std::size_t freeHeap, std::size_t largestBlock) {
  if (!https) return true;
  return freeHeap >= kTlsWorkingSetBytes && largestBlock >= kTlsContiguousBytes;
}

constexpr int64_t kTlsBootGraceMs = 15000;
constexpr int64_t kFirstLoopStaggerMs = 2000;

// Just after joining the network, memory is still settling and a TLS fetch that would fit a
// minute later does not. Hold it rather than report a failure the user cannot act on.
inline bool tlsBootGraceActive(int64_t connectedForMs) {
  return connectedForMs < kTlsBootGraceMs;
}

constexpr int kMaxTightRetries = 5;
constexpr int64_t kTightRetryDelayMs = 4000;

inline bool shouldRetryTightFetch(int tries) { return tries < kMaxTightRetries; }

constexpr long kHttpTimeoutMs = 30000;
static_assert(kTlsBootGraceMs < kHttpTimeoutMs,
              "a held fetch must still fit the answer window");
static_assert(kMaxTightRetries * kTightRetryDelayMs < kHttpTimeoutMs,
              "retries must give up before the request expires");
#define AWTRIX_MAX_MQTT_SUBS 8
constexpr std::size_t kMaxMqttSubs = AWTRIX_MAX_MQTT_SUBS;

struct HttpRequest {
  uint32_t id = 0;
  std::string method;
  std::string url;
  std::string body;
  HttpHeaders headers;
  std::size_t maxBytes = 0;
  std::string find;
  std::size_t keep = 0;
};

struct HttpResult {
  uint32_t id = 0;
  bool ok = false;
  int status = 0;
  std::string body;
};

class IScriptHttp {
 public:
  virtual ~IScriptHttp() = default;
  virtual bool request(const HttpRequest& req) = 0;
};

class IScriptMqtt {
 public:
  virtual ~IScriptMqtt() = default;
  virtual void publish(const std::string& topic, const std::string& payload) = 0;
  virtual void subscribe(const std::string& topic) = 0;
  virtual void unsubscribeAll(const std::string& topic) = 0;
};

struct MqttMessage {
  std::string topic;
  std::string payload;
  std::string filter;
};

class IScriptIcon {
 public:
  virtual ~IScriptIcon() = default;
  virtual bool draw(Canvas& canvas, const std::string& name, int x, int y, int64_t nowMs) = 0;
};

class IScriptStoreSink {
 public:
  virtual ~IScriptStoreSink() = default;
  virtual void storeChanged(const std::string& script, const std::string& json) = 0;
};

enum class SoundAction : uint8_t { Play, Rtttl, Stop };

// Everything the script layer may reach outside itself. Any member may be null or empty, and
// a binding whose service is missing answers "not available" rather than failing.
struct ScriptServices {
  IScriptHttp* http = nullptr;
  IScriptMqtt* mqtt = nullptr;
  IScriptIcon* icon = nullptr;
  IScriptStoreSink* storeSink = nullptr;
  SharedState* shared = nullptr;
  const EffectRegistry* effects = nullptr;
  const EffectRegistry* overlays = nullptr;
  const GfxFont* fonts[kFontCount] = {nullptr, nullptr};
  const Canvas* panel = nullptr;
  std::function<int64_t()> monotonicMs;
  std::function<void(const std::string&)> log;
  std::function<std::size_t()> freeHeap;
  std::function<std::size_t()> maxAllocHeap;
  std::function<void(const std::string&)> logDebug;
  std::function<bool(const std::string& json)> notify;
  std::function<const Settings*()> settings;
  std::function<const RuntimeState*()> runtime;
  std::function<bool(const std::string& json)> setSettings;
  std::function<bool(SoundAction, const std::string&)> sound;
  std::function<bool()> soundPlaying;
  std::function<void()> rotateNext;
  std::function<void()> rotatePrevious;
  std::function<bool(const std::string&)> showApp;
  std::function<void(bool)> holdRotation;
  std::function<bool(const std::string& name, std::string& out)> readSource;
  std::function<bool(const std::string& name, std::string& out)> readStore;
};

}
