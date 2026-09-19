#include "system/ScriptHttpWorker.h"

#include <WiFiClient.h>
#include "core/script/ModbusTcp.h"
#include "system/MonotonicClock.h"

namespace awtrix {
void ScriptHttpWorker::fetchModbus(const script::HttpRequest& req) {
  script::HttpResult result;
  result.id = req.id;
  script::modbus::Read read;
  if (req.method == "GET" && script::modbus::parse(req.url, read)) {
    WiFiClient client;
    client.setTimeout(2);
    if (client.connect(read.host.c_str(), read.port, 2000)) {
      result = script::modbus::exchange(client, read, req.id,
          [] { return monotonicMs(); }, [] { vTaskDelay(1); });
    }
    client.stop();
  }
  onResult_(std::move(result));
}
}
