#include "wifi_manager.h"

#include <time.h>
#include "cyd_config.h"

void WifiManager::begin() {
  preferences_.begin("filehub", false);
  apSsid_ = AppConfig::kDefaultApSsid;
  apPassword_ = AppConfig::kDefaultApPassword;
  preferences_.putString("ap_ssid", apSsid_);
  preferences_.putString("ap_pass", apPassword_);
  stationSsid_ = preferences_.getString("sta_ssid", "");
  stationPassword_ = preferences_.getString("sta_pass", "");
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.setHostname("esp32-file-hub");
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  Serial.printf("[WiFi] Provisioning AP: %s\n", apSsid_.c_str());
  startSavedMode();
}

void WifiManager::startCaptiveDns() {
  if (dnsStarted_) dnsServer_.stop();
  dnsServer_.setErrorReplyCode(DNSReplyCode::NoError);
  dnsStarted_ = dnsServer_.start(53, "*", WiFi.softAPIP());
  if (!dnsStarted_) Serial.println("[WiFi] Failed to start captive DNS");
}

void WifiManager::startSavedMode() {
  const String savedMode = preferences_.getString(
      "wifi_mode", stationSsid_.isEmpty() ? "ap" : "sta");
  const bool useStation = savedMode == "sta" && !stationSsid_.isEmpty();
  WiFi.mode(useStation ? WIFI_AP_STA : WIFI_AP);
  if (!WiFi.softAP(apSsid_.c_str(), apPassword_.c_str())) {
    Serial.println("[WiFi] Failed to start provisioning access point");
  } else {
    startCaptiveDns();
  }
  if (useStation) {
    stationMode_ = true;
    WiFi.begin(stationSsid_.c_str(), stationPassword_.c_str());
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    Serial.printf("[WiFi] Connecting to saved network: %s\n",
                  stationSsid_.c_str());
  } else {
    stationMode_ = false;
  }
}

void WifiManager::tick() {
  if (dnsStarted_) dnsServer_.processNextRequest();
  if (!stationMode_ || WiFi.status() == WL_CONNECTED) return;
  if (millis() - lastReconnect_ < 15000) return;
  lastReconnect_ = millis();
  Serial.println("[WiFi] Reconnecting to station");
  WiFi.disconnect(false, false);
  WiFi.begin(stationSsid_.c_str(), stationPassword_.c_str());
}

bool WifiManager::configureStation(const String& ssid, const String& password,
                                   String& error) {
  if (ssid.isEmpty() || ssid.length() > 32 || password.length() > 63) {
    error = "SSID must be 1-32 characters; password must be at most 63";
    return false;
  }
  for (size_t i = 0; i < ssid.length(); ++i) {
    if (static_cast<uint8_t>(ssid[i]) < 32) {
      error = "SSID contains invalid characters";
      return false;
    }
  }
  stationSsid_ = ssid;
  stationPassword_ = password;
  preferences_.putString("sta_ssid", stationSsid_);
  preferences_.putString("sta_pass", stationPassword_);
  preferences_.putString("wifi_mode", "sta");
  stationMode_ = true;
  lastReconnect_ = millis();
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(stationSsid_.c_str(), stationPassword_.c_str());
  return true;
}

bool WifiManager::startAccessPoint(String& error) {
  WiFi.disconnect(false, false);
  preferences_.putString("wifi_mode", "ap");
  stationMode_ = false;
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(apSsid_.c_str(), apPassword_.c_str())) {
    error = "Unable to start access point";
    return false;
  }
  startCaptiveDns();
  return true;
}

String WifiManager::ipAddress() const {
  return connected() ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
}

String WifiManager::modeName() const {
  return connected() ? "STA + AP" : "AP";
}

bool WifiManager::connected() const { return WiFi.status() == WL_CONNECTED; }
String WifiManager::ssid() const {
  return connected() ? WiFi.SSID() : apSsid_;
}
String WifiManager::stationSsid() const { return stationSsid_; }
int WifiManager::rssi() const { return connected() ? WiFi.RSSI() : 0; }
String WifiManager::dashboardUrl() const { return "http://" + ipAddress(); }
String WifiManager::apSsid() const { return apSsid_; }
String WifiManager::apPassword() const { return apPassword_; }
String WifiManager::accessPointUrl() const {
  return "http://" + WiFi.softAPIP().toString() + "/";
}
