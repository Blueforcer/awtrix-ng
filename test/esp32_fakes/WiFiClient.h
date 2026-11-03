#pragma once

class WiFiClient {
 public:
  virtual ~WiFiClient() = default;
  void setTimeout(int) {}
};
