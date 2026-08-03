#include "transport/net/NetworkService.h"

#include <ESPmDNS.h>
#include <WiFi.h>
#include <esp_wifi.h>

#include <cstring>

#include "core/net/HostName.h"
#include "core/net/WifiLink.h"
#include "system/Log.h"

namespace awtrix {

namespace {
IPAddress parseIp(const std::string& s) {
  IPAddress a;
  a.fromString(s.c_str());
  return a;
}
constexpr unsigned long kApRetryMs = 30000;
constexpr unsigned long kCheckMs = 5000;
constexpr int kWeakChecksBeforeRoam = 6;
constexpr unsigned long kRoamCooldownMs = 300000;

net::WifiAssoc assocNow(bool apMode) {
  // In provisioning mode the station side is only ever mid-retry: a successful join restarts the
  // device, so "connected" is not a state the AP branch ever has to report.
  if (apMode) return net::WifiAssoc::Disconnected;
  switch (WiFi.status()) {
    case WL_CONNECTED:      return net::WifiAssoc::Connected;
    case WL_NO_SSID_AVAIL:  return net::WifiAssoc::NoSsidFound;
    case WL_CONNECT_FAILED: return net::WifiAssoc::AuthFailed;
    case WL_IDLE_STATUS:
    case WL_SCAN_COMPLETED: return net::WifiAssoc::Idle;
    default:                return net::WifiAssoc::Disconnected;
  }
}
}

void NetworkService::publishStatus() {
  if (!status_) return;
  const bool hasSsid = cfg_ && !cfg_->wifiSsid.empty();
  net::applyWifiAssoc(*status_, assocNow(apMode_), hasSsid,
                      hasSsid ? cfg_->wifiSsid : std::string(),
                      std::string(WiFi.localIP().toString().c_str()));
}

void NetworkService::begin(const DeviceConfig& cfg, bool forceAp,
                           const std::function<void()>& onWait) {
  hostname_ = net::effectiveHostname(cfg.hostname, WiFi.macAddress().c_str());
  WiFi.persistent(true);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(hostname_.c_str());
  // Widest legal channel set (1-13) so an AP on 12 or 13 is visible; the regulatory domain is
  // corrected from the AP's country IE once we associate.
  wifi_country_t country = {};
  memcpy(country.cc, "CN", 3);
  country.schan = 1;
  country.nchan = 13;
  country.policy = WIFI_COUNTRY_POLICY_AUTO;
  esp_wifi_set_country(&country);
  // Modem sleep adds a hundred milliseconds of latency to every packet, which shows up as stuttery
  // Art-Net and laggy API calls. The device is mains-powered, so trade the power for latency.
  esp_wifi_set_ps(WIFI_PS_NONE);
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);

  if (cfg.netStatic && !cfg.ip.empty()) {
    WiFi.config(parseIp(cfg.ip), parseIp(cfg.gateway), parseIp(cfg.subnet),
                cfg.dns1.empty() ? parseIp(cfg.gateway) : parseIp(cfg.dns1),
                cfg.dns2.empty() ? IPAddress(0, 0, 0, 0) : parseIp(cfg.dns2));
  }

  cfg_ = &cfg;
  const unsigned long timeoutMs =
      cfg.wifiConnectTimeout > 0 ? static_cast<unsigned long>(cfg.wifiConnectTimeout) : 15000UL;
  if (!forceAp && !cfg.wifiSsid.empty()) {
    WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPass.c_str());
    if (status_) net::applyWifiAssoc(*status_, net::WifiAssoc::Joining, true, cfg.wifiSsid, "");
    const unsigned long start = millis();
    // Blocks boot until the join succeeds or times out; onWait keeps the boot animation moving so
    // the matrix does not look frozen.
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeoutMs) {
      if (onWait) onWait();
      delay(10);
    }
  }

  if (forceAp || WiFi.status() != WL_CONNECTED) {
    apMode_ = true;
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(hostname_.c_str());
    dns_.setErrorReplyCode(DNSReplyCode::NoError);
    dns_.start(53, "*", WiFi.softAPIP());
    logf("wifi: %s, provisioning AP \"%s\" at %s (captive portal)",
         forceAp ? "forced by button" : "no connection", hostname_.c_str(),
         WiFi.softAPIP().toString().c_str());
  } else {
    apMode_ = false;
    logf("wifi: connected to \"%s\" (%d dBm) as %s", WiFi.SSID().c_str(), WiFi.RSSI(),
         WiFi.localIP().toString().c_str());
    logf("heap: %u KB free with radio up",
         (unsigned)(heap_caps_get_free_size(MALLOC_CAP_DEFAULT) / 1024));
    if (MDNS.begin(hostname_.c_str())) {
      String mac = WiFi.macAddress();
      mac.replace(":", "");
      mac.toLowerCase();
      const uint16_t port = cfg.webPort > 0 ? static_cast<uint16_t>(cfg.webPort) : 80;
      MDNS.addService("http", "tcp", port);
      MDNS.addService("awtrixng", "tcp", port);
      MDNS.addServiceTxt("awtrixng", "tcp", "id", mac.c_str());
      MDNS.addServiceTxt("awtrixng", "tcp", "name", hostname_.c_str());
      MDNS.addServiceTxt("awtrixng", "tcp", "type", "awtrixng");
    }
  }

  publishStatus();
  // The join loop above gives up silently, so nothing else would record why boot ended offline.
  if (status_ && status_->phase == net::LinkPhase::Offline &&
      status_->error == net::LinkError::None && !forceAp)
    status_->setError(net::LinkError::Timeout);
}

void NetworkService::tick() {
  if (apMode_) {
    dns_.processNextRequest();
    retryJoinFromAp();
    return;
  }
  const unsigned long nowMs = millis();
  if (nowMs - lastCheckMs_ < kCheckMs) return;
  lastCheckMs_ = nowMs;
  publishStatus();
  if (WiFi.status() != WL_CONNECTED) {
    logf("wifi: connection lost, reconnecting");
    weakChecks_ = 0;
    WiFi.reconnect();
    if (status_) net::noteWifiRetry(*status_, kCheckMs);
    return;
  }
  roamIfWeak(nowMs);
}

// Forces a re-association when the signal stays below the configured threshold, since the ESP32
// otherwise clings to a weak AP indefinitely. Requires several bad samples plus a long cooldown so
// a passing dip cannot start flapping.
void NetworkService::roamIfWeak(unsigned long nowMs) {
  if (!cfg_ || cfg_->wifiRoamRssi >= 0) return;
  if (nowMs - lastRoamMs_ < kRoamCooldownMs) return;
  if (WiFi.RSSI() >= cfg_->wifiRoamRssi) {
    weakChecks_ = 0;
    return;
  }
  if (++weakChecks_ < kWeakChecksBeforeRoam) return;
  weakChecks_ = 0;
  lastRoamMs_ = nowMs;
  logf("wifi: %d dBm below the %d dBm roam threshold, looking for a stronger AP",
       static_cast<int>(WiFi.RSSI()), cfg_->wifiRoamRssi);
  WiFi.reconnect();
}

// In provisioning mode, keep trying the stored credentials so the device recovers on its own once
// the router comes back. Skipped while someone is attached to the AP, because a join attempt
// disrupts the portal they are using.
void NetworkService::retryJoinFromAp() {
  if (!cfg_ || cfg_->wifiSsid.empty()) return;
  if (WiFi.softAPgetStationNum() > 0) return;
  const unsigned long now = millis();
  if (now - lastApRetryMs_ < kApRetryMs) return;
  lastApRetryMs_ = now;
  if (WiFi.status() != WL_CONNECTED) {
    publishStatus();
    if (status_) net::noteWifiRetry(*status_, kApRetryMs);
    WiFi.begin(cfg_->wifiSsid.c_str(), cfg_->wifiPass.c_str());
    return;
  }
  logf("wifi: joined \"%s\" from provisioning mode, restarting to leave the AP",
       WiFi.SSID().c_str());
  if (onJoinedFromAp_) onJoinedFromAp_();
}

bool NetworkService::isConnected() const { return !apMode_ && WiFi.status() == WL_CONNECTED; }

std::string NetworkService::ip() const {
  return std::string((apMode_ ? WiFi.softAPIP() : WiFi.localIP()).toString().c_str());
}

}
