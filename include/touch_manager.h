#pragma once

#include <Arduino.h>
#include <XPT2046_Touchscreen.h>
#include "cyd_config.h"
#include "display_manager.h"
#include "file_manager.h"
#include "wifi_manager.h"
#include <vector>

class TouchManager {
 public:
  void begin();
  void tick(DisplayManager& display, WifiManager& wifi, FileManager& files);
  uint8_t page() const;
  String galleryPath() const;
  String filePath() const;
  bool wifiQr() const;
  bool overlayVisible() const;

 private:
  void refreshFiles(FileManager& files);
  void refreshGallery(FileManager& files);
  uint8_t page_ = 0;
  uint32_t lastTouchMs_ = 0;
  uint32_t restartTouchStart_ = 0;
  uint32_t touchCandidateSince_ = 0;
  uint32_t deleteHoldSince_ = 0;
  bool touchHeld_ = false;
  bool deleteHoldPending_ = false;
  bool calibrationMode_ = false;
  bool imageTestMode_ = false;
  uint8_t calibrationPoint_ = 0;
  int16_t calibrationRawX_[4] = {};
  int16_t calibrationRawY_[4] = {};
  int16_t calibrationMinX_ = 250;
  int16_t calibrationMaxX_ = 3850;
  int16_t calibrationMinY_ = 200;
  int16_t calibrationMaxY_ = 3850;
  uint16_t sampleRawX_[5] = {};
  uint16_t sampleRawY_[5] = {};
  uint8_t sampleCount_ = 0;
  uint8_t sampleIndex_ = 0;
  uint16_t lastCandidateX_ = 0;
  uint16_t lastCandidateY_ = 0;
  uint16_t lastRawX_ = 0;
  uint16_t lastRawY_ = 0;
  String galleryPath_ = "/Albums";
  String filePath_ = "/";
  String albumName_;
  bool albumInputMode_ = false;
  bool wifiKeyboardMode_ = false;
  bool wifiKeyboardPassword_ = false;
  bool wifiKeyboardSymbols_ = false;
  bool wifiKeyboardUppercase_ = false;
  String wifiSsid_;
  String wifiKeyboardValue_;
  XPT2046_Touchscreen touch_{CydPins::kTouchCs, CydPins::kTouchIrq};
  bool deleteConfirm_ = false;
  bool deleteFromGallery_ = false;
  bool deleteWasPhoto_ = false;
  String pendingDeletePath_;
  uint16_t fileOffset_ = 0;
  uint16_t galleryOffset_ = 0;
  uint16_t galleryCount_ = 0;
  uint16_t fileCount_ = 0;
  String photoPath_;
  std::vector<String> galleryItems_;
  std::vector<bool> galleryDirectories_;
  size_t photoIndex_ = 0;
  size_t imageTestIndex_ = 0;
  String imageTestResult_ = "Select LOAD to test the first supported image.";
};
