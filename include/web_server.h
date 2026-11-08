#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include "display_manager.h"
#include "file_manager.h"
#include "wifi_manager.h"

class WebServerManager {
 public:
  WebServerManager(FileManager& files, WifiManager& wifi,
                   DisplayManager& display);
  void begin();
  void tick();

 private:
  void registerRoutes();
  bool authorize(AsyncWebServerRequest* request);
  bool validSession(AsyncWebServerRequest* request);
  void expireSession();
  void handleUpload(AsyncWebServerRequest* request, String filename, size_t index,
                    uint8_t* data, size_t len, bool final);
  void handleOta(AsyncWebServerRequest* request, String filename, size_t index,
                 uint8_t* data, size_t len, bool final);
  String createToken();

  FileManager& files_;
  WifiManager& wifi_;
  DisplayManager& display_;
  AsyncWebServer server_{80};
  String sessionToken_;
  uint32_t lastActivity_ = 0;
  uint32_t restartAt_ = 0;
  bool otaInProgress_ = false;
  bool routesRegistered_ = false;
};
