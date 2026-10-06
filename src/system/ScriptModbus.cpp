#include "system/ScriptHttpWorker.h"

#include <WiFiClient.h>
#include "core/script/ModbusTcp.h"
#include "system/MonotonicClock.h"
#include "system/Log.h"

namespace awtrix {
void ScriptHttpWorker::fetchModbus(const script::HttpRequest& req) {
  script::HttpResult result;
  result.id = req.id;
  const auto started = monotonicMs();
  const char* diagnostic = "invalid request";
  script::modbus::Read read;
  if (req.method == "GET" && script::modbus::parse(req.url, read)) {
    WiFiClient client;
    client.setTimeout(2);
    diagnostic = "connection failed";
    if (client.connect(read.host.c_str(), read.port, 2000)) {
      result = script::modbus::exchange(client, read, req.id,
          [] { return monotonicMs(); }, [] { vTaskDelay(1); }, &diagnostic);
    }
    client.stop();
  }
  if (!result.ok) {
    const std::string report = script::modbus::failureReport(
        read, diagnostic, result.status, static_cast<unsigned long>(monotonicMs() - started));
    logf("%s", report.c_str());
  }
  onResult_(std::move(result));
}
}
