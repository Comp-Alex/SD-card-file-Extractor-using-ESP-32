#pragma once

#include <Arduino.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <WiFi.h>

class WifiManager {
 public:
  void begin();
  void tick();
  bool configureStation(const String& ssid, const String& password, String& error);
  bool startAccessPoint(String& error);
  String ipAddress() const;
  String modeName() const;
  bool connected() const;
  String ssid() const;
  String stationSsid() const;
  int rssi() const;
  String dashboardUrl() const;
  String apSsid() const;
  String apPassword() const;
  String accessPointUrl() const;

 private:
  void startSavedMode();
  Preferences preferences_;
  DNSServer dnsServer_;
  String apSsid_;
  String apPassword_;
  String stationSsid_;
  String stationPassword_;
  bool stationMode_ = false;
  bool dnsStarted_ = false;
  uint32_t lastReconnect_ = 0;
  void startCaptiveDns();
};
