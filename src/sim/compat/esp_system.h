#pragma once


enum esp_reset_reason_t {
  ESP_RST_UNKNOWN = 0,
  ESP_RST_POWERON,
  ESP_RST_EXT,
  ESP_RST_SW,
  ESP_RST_PANIC,
  ESP_RST_INT_WDT,
  ESP_RST_TASK_WDT,
  ESP_RST_WDT,
  ESP_RST_DEEPSLEEP,
  ESP_RST_BROWNOUT,
  ESP_RST_SDIO,
};

// Starting the binary is always a clean power-on; there is no reset cause to read on the host.
inline esp_reset_reason_t esp_reset_reason() { return ESP_RST_POWERON; }
