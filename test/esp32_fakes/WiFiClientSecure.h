#pragma once

#include <WiFiClient.h>

class WiFiClientSecure : public WiFiClient {
 public:
  void setInsecure() {}
};
