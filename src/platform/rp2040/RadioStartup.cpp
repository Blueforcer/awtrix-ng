#include "platform/rp2040/RadioStartup.h"
#include <hardware/pio.h>
#include <WiFi.h>
#include "system/Log.h"
#include <cyw43.h>

// The Pico W variants call this before setup(). Defer it until board->begin()
// has claimed the display SM/program and DMA channels. Do not override the
// entire variant (which would discard future board startup work).
extern "C" void __wrap_init_cyw43_wifi() {}
extern "C" void __real_init_cyw43_wifi();

namespace {
// Single producer (the driver's async context), single consumer (the main loop), one core.
constexpr uint32_t kEventSlots = 32;
awtrix::platform::RadioEvent events[kEventSlots];
volatile uint32_t eventHead = 0;  // written by the producer
volatile uint32_t eventTail = 0;  // written by the consumer
volatile uint32_t eventsDropped = 0;
}

// The driver's own trace (CYW43_TRACE_ASYNC_EV) prints from interrupt context through printf,
// which arduino-pico discards. Record the events here and log them from the main loop instead.
extern "C" void __real_cyw43_cb_process_async_event(void* cb_data, const cyw43_async_event_t* ev);
extern "C" void __wrap_cyw43_cb_process_async_event(void* cb_data, const cyw43_async_event_t* ev) {
  __real_cyw43_cb_process_async_event(cb_data, ev);
  if (ev->event_type == CYW43_EV_ESCAN_RESULT) return;
  const uint32_t head = eventHead;
  if (head - eventTail >= kEventSlots) {
    eventsDropped = eventsDropped + 1;
    return;
  }
  awtrix::platform::RadioEvent& e = events[head % kEventSlots];
  e.ms = millis();
  e.type = ev->event_type;
  e.status = ev->status;
  e.reason = ev->reason;
  e.flags = ev->flags;
  e.itf = ev->interface;
  // wl_event_msg_t after `reason`: auth_type(4), datalen(4), addr(6).
  memcpy(e.addr, ev->_1 + 8, sizeof e.addr);
  e.joinState = static_cast<const cyw43_t*>(cb_data)->wifi_join_state;
  __sync_synchronize();
  eventHead = head + 1;
}

namespace awtrix::platform {
bool takeRadioEvent(RadioEvent& out, uint32_t& dropped) {
  dropped = eventsDropped;
  const uint32_t tail = eventTail;
  if (tail == eventHead) return false;
  __sync_synchronize();
  out = events[tail % kEventSlots];
  __sync_synchronize();
  eventTail = tail + 1;
  return true;
}

const char* radioEventName(uint32_t type) {
  switch (type) {
    case CYW43_EV_SET_SSID:         return "SET_SSID";
    case CYW43_EV_JOIN:             return "JOIN";
    case CYW43_EV_AUTH:             return "AUTH";
    case CYW43_EV_DEAUTH:           return "DEAUTH";
    case CYW43_EV_DEAUTH_IND:       return "DEAUTH_IND";
    case CYW43_EV_ASSOC:            return "ASSOC";
    case 9:                         return "REASSOC";  // WLC_E_REASSOC: firmware roam
    case 32:                        return "ROAM_PREP";  // WLC_E_ROAM_PREP
    case CYW43_EV_DISASSOC:         return "DISASSOC";
    case CYW43_EV_DISASSOC_IND:     return "DISASSOC_IND";
    case CYW43_EV_LINK:             return "LINK";
    case CYW43_EV_PRUNE:            return "PRUNE";
    case CYW43_EV_PSK_SUP:          return "PSK_SUP";
    case CYW43_EV_ICV_ERROR:        return "ICV_ERROR";
    case CYW43_EV_CSA_COMPLETE_IND: return "CSA_COMPLETE_IND";
    case CYW43_EV_ASSOC_REQ_IE:     return "ASSOC_REQ_IE";
    case CYW43_EV_ASSOC_RESP_IE:    return "ASSOC_RESP_IE";
    default:                        return nullptr;
  }
}

int disableFirmwareRoaming() {
  uint8_t set[] = {'r', 'o', 'a', 'm', '_', 'o', 'f', 'f', 0, 1, 0, 0, 0};
  if (cyw43_ioctl(&cyw43_state, CYW43_IOCTL_SET_VAR, sizeof set, set, CYW43_ITF_STA) != 0) return -1;
  return firmwareRoamOff();
}

int firmwareRoamOff() {
  uint8_t get[] = {'r', 'o', 'a', 'm', '_', 'o', 'f', 'f', 0, 0, 0, 0, 0};
  if (cyw43_ioctl(&cyw43_state, CYW43_IOCTL_GET_VAR, sizeof get, get, CYW43_ITF_STA) != 0) return -1;
  return get[0] | (get[1] << 8) | (get[2] << 16) | (get[3] << 24);
}

void beginRadio() {
  static bool started = false;
  if (started) return;
  started = true;
  bool claimed[NUM_PIOS][NUM_PIO_STATE_MACHINES]{};
  for (uint p = 0; p < NUM_PIOS; ++p)
    for (uint sm = 0; sm < NUM_PIO_STATE_MACHINES; ++sm)
      claimed[p][sm] = pio_sm_is_claimed(pio_get_instance(p), sm);
  __real_init_cyw43_wifi();
  // cyw43_init alone initializes software state; the SPI bus/PIO allocation
  // is lazy. Reading the MAC brings the interface up before the second snapshot.
  WiFi.macAddress();
  // Query the SDK allocation bitmap, not a guessed SM or private bus_data ABI.
  // No other allocator runs between these snapshots on this core.
  bool found = false;
  for (uint p = 0; p < NUM_PIOS; ++p) {
    for (uint sm = 0; sm < NUM_PIO_STATE_MACHINES; ++sm) {
      if (!claimed[p][sm] && pio_sm_is_claimed(pio_get_instance(p), sm)) {
        logf("wifi: CYW43 claimed PIO%u SM%u after display startup", p, sm);
        found = true;
      }
    }
  }
  if (!found) logf("wifi: CYW43 PIO allocation not observed; check radio initialization");
}
}
