#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "file_manager.h"
#include "wifi_manager.h"

class DisplayManager {
 public:
  void begin();
  bool readTouch(uint16_t& x, uint16_t& y, uint16_t& pressure,
                 uint16_t& rawX, uint16_t& rawY);
  void drawPage(uint8_t page, WifiManager& wifi, FileManager& files,
                bool wifiQr = false, const String& galleryPath = "/Albums",
                const String& filePath = "/");
  void refreshStatusBar(WifiManager& wifi, FileManager& files);
  void refreshTransfer();
  void drawWifiKeyboard(const String& field, const String& value,
                        bool symbols, bool uppercase);
  void drawWifiNotice(const String& message);
  void drawGallery(FileManager& files, const String& path);
  bool showPhoto(FileManager& files, const String& path,
                 bool testMode = false);
  void drawImageTestPage(bool sdMounted, uint32_t freeHeap,
                         uint32_t largestBlock, const String& imagePath,
                         const String& result);
  void showTouchCalibration(uint8_t point);
  void finishTouchCalibration();
  void drawGalleryMessage(const String& message);
  void invalidateThumbnails();
  void closePhoto();
  bool photoVisible() const;
  void drawAlbumKeyboard(const String& albumName);
  void drawDeleteConfirmation(const String& path);
  void setFileOffset(uint16_t offset);
  void setGalleryOffset(uint16_t offset);
  void setBrightness(uint8_t percent);
  uint8_t brightness() const;

 private:
  void drawStatusBar(WifiManager& wifi, FileManager& files);
  void drawNavigation(uint8_t page);
  void drawHome(WifiManager& wifi, FileManager& files);
  void drawFiles(FileManager& files, const String& path);
  void drawTransfer();
  void drawSettings(WifiManager& wifi, bool wifiQr);
  void drawPhotoControls();
  void drawPhotoError(const String& path, const String& message);
  TFT_eSPI tft_;
  uint8_t brightness_ = 80;
  uint8_t lastPage_ = 255;
  String currentPhoto_;
  bool photoVisible_ = false;
  bool photoTestMode_ = false;
  uint16_t fileOffset_ = 0;
  uint16_t galleryOffset_ = 0;
  struct Thumbnail {
    String path;
    uint16_t pixels[30 * 18] = {};
    bool valid = false;
  };
  Thumbnail thumbnails_[5];
};
