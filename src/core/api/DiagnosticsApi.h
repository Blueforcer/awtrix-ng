#pragma once

#include "core/api/HttpExchange.h"
#include "core/api/JsonStream.h"

namespace awtrix::api {

class WifiScanWriter {
 public:
  explicit WifiScanWriter(JsonStream& output) : output_(output) { output_.put('['); }
  void network(const char* ssid, int rssi, bool secure);
  void end();
 private:
  JsonStream& output_;
  bool first_ = true;
};

HttpResult wifiScanPending();
void replyChunk(void* reply, const char* data, std::size_t size);
using WifiScanFn = std::function<void(Reply& reply)>;
using LogsFn = void (*)(uint32_t after, JsonStream& output);
bool routeDiagnostics(const Request& request, Reply& reply, const WifiScanFn& scan, LogsFn logs);

}
