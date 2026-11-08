#include <Arduino.h>
#include <Preferences.h>
#include "display_manager.h"
#include "file_manager.h"
#include "touch_manager.h"
#include "transfer_state.h"
#include "web_server.h"
#include "wifi_manager.h"

namespace {
FileManager fileManager;
WifiManager wifiManager;
DisplayManager displayManager;
TouchManager touchManager;
WebServerManager webServer(fileManager, wifiManager, displayManager);
bool uiTaskRunning = false;

void uiTask(void*) {
  uint32_t lastStatusRefresh = 0;
  uint32_t lastTransferRefresh = 0;
  for (;;) {
    touchManager.tick(displayManager, wifiManager, fileManager);
    const uint32_t now = millis();
    const TransferState transfer = getTransferState();
    if (!transfer.active && !touchManager.overlayVisible() &&
        !displayManager.photoVisible()) {
      if (now - lastStatusRefresh >= 1000) {
        displayManager.refreshStatusBar(wifiManager, fileManager);
        lastStatusRefresh = now;
      }
      if (touchManager.page() == 3 && now - lastTransferRefresh >= 250) {
        displayManager.refreshTransfer();
        lastTransferRefresh = now;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(15));
  }
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println();
  Serial.println("[BOOT] ESP32 File Hub");

  wifiManager.begin();
  displayManager.begin();
  touchManager.begin();
  const bool storageReady = fileManager.begin();
  Serial.printf("[Gallery] SD mount check: %s\n",
                storageReady && fileManager.isMounted() ? "mounted"
                                                        : "unavailable");
  if (fileManager.isMounted()) {
    String error;
    if (!fileManager.makeDirectory("/Albums", error) &&
        error != "Path already exists") {
      Serial.printf("[Gallery] Could not initialize Albums folder: %s\n",
                    error.c_str());
    }
  }

  Preferences preferences;
  preferences.begin("filehub", true);
  const uint8_t brightness = preferences.getUChar("brightness", 80);
  preferences.end();
  displayManager.setBrightness(brightness);

  webServer.begin();
  displayManager.drawPage(0, wifiManager, fileManager, touchManager.wifiQr(),
                          touchManager.galleryPath(),
                          touchManager.filePath());

  const BaseType_t taskCreated =
      xTaskCreatePinnedToCore(uiTask, "display-ui", 6144, nullptr, 1, nullptr, 1);
  uiTaskRunning = taskCreated == pdPASS;
  if (!uiTaskRunning) {
    Serial.println("[BOOT] Unable to create UI task; running UI from loop");
  }
}

void loop() {
  static uint32_t lastSdCheck = 0;
  wifiManager.tick();
  webServer.tick();
  if (millis() - lastSdCheck >= 1000) {
    lastSdCheck = millis();
    fileManager.retryMount();
  }
  if (!uiTaskRunning) {
    touchManager.tick(displayManager, wifiManager, fileManager);
  }
  vTaskDelay(pdMS_TO_TICKS(uiTaskRunning ? 10 : 15));
}
