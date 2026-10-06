#include "core/api/DiagnosticsApi.h"

#include <cstdlib>

namespace awtrix::api {

void WifiScanWriter::network(const char* ssid, int rssi, bool secure) {
  if (!first_) output_.put(',');
  first_ = false;
  output_.put("{\"ssid\":");
  output_.putString(ssid);
  output_.put(",\"rssi\":");
  output_.putInt(rssi);
  output_.put(",\"enc\":");
  output_.put(secure ? "true}" : "false}");
}

void WifiScanWriter::end() { output_.put(']'); output_.flush(); }

HttpResult wifiScanPending() { return {202, "application/json", "{\"scanning\":true}"}; }

void replyChunk(void* reply, const char* data, std::size_t size) {
  static_cast<Reply*>(reply)->chunk(data, size);
}

bool routeDiagnostics(const Request& request, Reply& reply, const WifiScanFn& scan, LogsFn logs) {
  if (request.method != "GET") return false;
  if (request.path == "/api/v1/system/wifi-scan") {
    if (scan) scan(reply);
    else reply.send({200, "application/json", "[]"});
    return true;
  }
  if (request.path != "/api/v1/logs") return false;
  const auto after = static_cast<uint32_t>(std::strtoul(request.parameter("after").c_str(), nullptr, 10));
  reply.send({200, "application/json", {}}, true);
  JsonStream output(replyChunk, &reply);
  logs(after, output);
  output.flush();
  reply.chunk("", 0);
  return true;
}

}
