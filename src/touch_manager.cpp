#include "touch_manager.h"

#include <ArduinoJson.h>
#include <Preferences.h>
#include <SPI.h>
#include "cyd_config.h"
#include "transfer_state.h"
#include <esp_heap_caps.h>

namespace {
bool showWifiQr = false;

String joinPath(const String& folder, const String& name) {
  return folder == "/" ? "/" + name : folder + "/" + name;
}

String parentPath(const String& path) {
  if (path == "/" || path == "/external" || path == "/internal") return "/";
  const int slash = path.lastIndexOf('/');
  if (slash <= 0) return "/";
  if (slash == 0) return "/";
  return path.substring(0, slash);
}

bool isImageFile(String path) {
  path.toLowerCase();
  return path.endsWith(".jpg") || path.endsWith(".jpeg") ||
         path.endsWith(".png") || path.endsWith(".bmp");
}

uint16_t trimmedAverage(const uint16_t values[5]) {
  uint16_t sorted[5];
  memcpy(sorted, values, sizeof(sorted));
  for (uint8_t i = 1; i < 5; ++i) {
    const uint16_t value = sorted[i];
    int8_t j = i - 1;
    while (j >= 0 && sorted[j] > value) {
      sorted[j + 1] = sorted[j];
      --j;
    }
    sorted[j + 1] = value;
  }
  return static_cast<uint16_t>((sorted[1] + sorted[2] + sorted[3]) / 3);
}
}  // namespace

void TouchManager::begin() {
  SPI.begin(CydPins::kTouchSclk, CydPins::kTouchMiso, CydPins::kTouchMosi,
            CydPins::kTouchCs);
  touch_.begin();
  SPI.begin(CydPins::kTouchSclk, CydPins::kTouchMiso, CydPins::kTouchMosi,
            CydPins::kTouchCs);
  touch_.setRotation(1);
  Preferences preferences;
  preferences.begin("touch", true);
  calibrationMinX_ = preferences.getShort("minX", calibrationMinX_);
  calibrationMaxX_ = preferences.getShort("maxX", calibrationMaxX_);
  calibrationMinY_ = preferences.getShort("minY", calibrationMinY_);
  calibrationMaxY_ = preferences.getShort("maxY", calibrationMaxY_);
  preferences.end();
}

void TouchManager::refreshFiles(FileManager& files) {
  fileCount_ = 0;
  bool ok = false;
  String error;
  const String json = files.listJson(filePath_, ok, error);
  if (!ok) return;
  DynamicJsonDocument doc(8192);
  if (!deserializeJson(doc, json)) fileCount_ = doc["count"] | 0;
  if (fileOffset_ >= fileCount_ && fileOffset_ > 0)
    fileOffset_ = ((fileCount_ - 1) / 5) * 5;
}

void TouchManager::refreshGallery(FileManager& files) {
  galleryItems_.clear();
  galleryDirectories_.clear();
  galleryCount_ = 0;
  Serial.printf("[Gallery] Scanning Folder: %s\n", galleryPath_.c_str());
  bool ok = false;
  String error;
  const String json = files.listJson(galleryPath_, ok, error);
  if (!ok) return;
  DynamicJsonDocument doc(8192);
  if (deserializeJson(doc, json)) return;
  for (JsonObject item : doc["entries"].as<JsonArray>()) {
    const bool directory = item["directory"].as<bool>();
    const String path = item["path"] | "";
    if (directory || isImageFile(path)) {
      galleryItems_.push_back(path);
      galleryDirectories_.push_back(directory);
      if (!directory)
        Serial.printf("[Gallery] Found Image: %s\n", path.c_str());
    }
  }
  galleryCount_ = galleryItems_.size();
  if (galleryCount_ == 0) {
    galleryOffset_ = 0;
  } else if (galleryOffset_ >= galleryCount_) {
    galleryOffset_ = ((galleryCount_ - 1) / 5) * 5;
  }
}

void TouchManager::tick(DisplayManager& display, WifiManager& wifi,
                        FileManager& files) {
  const TransferState transfer = getTransferState();
  uint16_t touchX = 0, touchY = 0, pressure = 0, rawX = 0, rawY = 0;
  bool samplePressed = false;
  if (!transfer.active && digitalRead(CydPins::kTouchIrq) == LOW &&
      files.beginTouchRead()) {
    const TS_Point point = touch_.getPoint();
    files.endTouchRead();
    pressure = max<int16_t>(0, point.z);
    rawX = point.x;
    rawY = point.y;
    samplePressed = pressure >= 400;
  }
  if (!samplePressed) {
    touchHeld_ = false;
    deleteHoldPending_ = false;
    deleteHoldSince_ = 0;
    touchCandidateSince_ = 0;
    sampleCount_ = 0;
    sampleIndex_ = 0;
    restartTouchStart_ = 0;
    return;
  }

  if (sampleCount_ == 0) touchCandidateSince_ = millis();
  else {
    if (abs(static_cast<int>(rawX) - lastRawX_) > 60 ||
        abs(static_cast<int>(rawY) - lastRawY_) > 60) {
      sampleCount_ = 0;
      sampleIndex_ = 0;
      touchCandidateSince_ = millis();
    }
  }
  lastRawX_ = rawX;
  lastRawY_ = rawY;
  sampleRawX_[sampleIndex_] = rawX;
  sampleRawY_[sampleIndex_] = rawY;
  sampleIndex_ = (sampleIndex_ + 1) % 5;
  if (sampleCount_ < 5) ++sampleCount_;
  if (sampleCount_ < 5 || millis() - touchCandidateSince_ < 60) return;

  const uint16_t filteredX = trimmedAverage(sampleRawX_);
  const uint16_t filteredY = trimmedAverage(sampleRawY_);
  touchX = constrain(map(filteredX, calibrationMinX_, calibrationMaxX_, 0, 320),
                     0, 319);
  touchY = constrain(map(filteredY, calibrationMinY_, calibrationMaxY_, 0, 240),
                     0, 239);
  const int x = constrain(touchX, 0, 319);
  const int y = constrain(touchY, 0, 239);

  if (!wifiKeyboardMode_ && !calibrationMode_ && !imageTestMode_ &&
      page_ == 4 && y >= 181 &&
      x >= 230) {
    if (restartTouchStart_ == 0) restartTouchStart_ = millis();
    else if (millis() - restartTouchStart_ >= 1200) ESP.restart();
    touchHeld_ = true;
    return;
  }
  restartTouchStart_ = 0;
  if (touchHeld_) {
    if (abs(x - static_cast<int>(lastCandidateX_)) > 5 ||
        abs(y - static_cast<int>(lastCandidateY_)) > 5) {
      if (deleteHoldPending_)
        Serial.println("[Touch] Delete hold cancelled by movement");
      deleteHoldPending_ = false;
    }
    if (deleteHoldPending_ && millis() - deleteHoldSince_ >= 800) {
      deleteHoldPending_ = false;
      deleteConfirm_ = true;
      display.drawDeleteConfirmation(pendingDeletePath_);
    }
    return;
  }
  touchHeld_ = true;
  lastCandidateX_ = x;
  lastCandidateY_ = y;
  Serial.printf("[Touch] Raw X=%u Raw Y=%u Pressure=%u\n", filteredX,
                filteredY, pressure);
  Serial.printf("[Touch] Filtered X=%u Y=%u\n", touchX, touchY);
  Serial.println("[Touch] Valid Touch");
  Serial.printf("[Touch] Debounced screen=%d,%d\n", x, y);
  if (millis() - lastTouchMs_ < 200) return;
  lastTouchMs_ = millis();

  if (calibrationMode_) {
    calibrationRawX_[calibrationPoint_] = filteredX;
    calibrationRawY_[calibrationPoint_] = filteredY;
    ++calibrationPoint_;
    if (calibrationPoint_ < 4) {
      display.showTouchCalibration(calibrationPoint_);
    } else {
      calibrationMinX_ = min(calibrationRawX_[0], calibrationRawX_[3]);
      calibrationMaxX_ = max(calibrationRawX_[1], calibrationRawX_[2]);
      calibrationMinY_ = min(calibrationRawY_[0], calibrationRawY_[1]);
      calibrationMaxY_ = max(calibrationRawY_[2], calibrationRawY_[3]);
      if (abs(calibrationMaxX_ - calibrationMinX_) < 500 ||
          abs(calibrationMaxY_ - calibrationMinY_) < 500) {
        calibrationMinX_ = 250;
        calibrationMaxX_ = 3850;
        calibrationMinY_ = 200;
        calibrationMaxY_ = 3850;
        calibrationPoint_ = 0;
        display.showTouchCalibration(calibrationPoint_);
        return;
      }
      Preferences preferences;
      preferences.begin("touch", false);
      preferences.putShort("minX", calibrationMinX_);
      preferences.putShort("maxX", calibrationMaxX_);
      preferences.putShort("minY", calibrationMinY_);
      preferences.putShort("maxY", calibrationMaxY_);
      preferences.end();
      calibrationMode_ = false;
      calibrationPoint_ = 0;
      display.finishTouchCalibration();
      delay(700);
      display.drawPage(page_, wifi, files, showWifiQr, galleryPath_, filePath_);
    }
    return;
  }

  if (imageTestMode_) {
    if (display.photoVisible()) {
      if (y < 32 && x < 75) {
        display.closePhoto();
        display.drawImageTestPage(
            files.isMounted(galleryPath_.startsWith("/external")),
            ESP.getFreeHeap(), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
            imageTestIndex_ < galleryItems_.size()
                ? galleryItems_[imageTestIndex_]
                : String(),
            imageTestResult_);
      } else if (y >= 178 && y < 209 && x >= 95 && x < 225) {
        display.closePhoto();
        display.drawImageTestPage(
            files.isMounted(galleryPath_.startsWith("/external")),
            ESP.getFreeHeap(), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
            imageTestIndex_ < galleryItems_.size()
                ? galleryItems_[imageTestIndex_]
                : String(),
            imageTestResult_);
      } else if (y >= 178 && y < 209 &&
                 (x < 95 || x >= 225) && !galleryItems_.empty()) {
        for (size_t attempts = 0; attempts < galleryItems_.size(); ++attempts) {
          imageTestIndex_ =
              x < 95
                  ? (imageTestIndex_ + galleryItems_.size() - 1) %
                        galleryItems_.size()
                  : (imageTestIndex_ + 1) % galleryItems_.size();
          if (!galleryDirectories_[imageTestIndex_] &&
              isImageFile(galleryItems_[imageTestIndex_])) {
            imageTestResult_ = "Loading " + galleryItems_[imageTestIndex_];
            display.showPhoto(files, galleryItems_[imageTestIndex_], true);
            return;
          }
        }
      }
      return;
    }

    if (y >= 180 && y < 208 && x < 150) {
      imageTestMode_ = false;
      display.drawPage(page_, wifi, files, showWifiQr, galleryPath_, filePath_);
    } else if (y >= 180 && y < 208 && x >= 180) {
      refreshGallery(files);
      bool found = false;
      for (size_t i = 0; i < galleryItems_.size(); ++i) {
        if (!galleryDirectories_[i] && isImageFile(galleryItems_[i])) {
          imageTestIndex_ = i;
          found = true;
          break;
        }
      }
      if (found) {
        imageTestResult_ = "Loading " + galleryItems_[imageTestIndex_];
        if (!display.showPhoto(files, galleryItems_[imageTestIndex_], true))
          imageTestResult_ = "Load failed; see Serial Monitor for decoder reason.";
      } else {
        imageTestResult_ = "No supported images in " + galleryPath_;
        display.drawImageTestPage(
            files.isMounted(galleryPath_.startsWith("/external")),
            ESP.getFreeHeap(), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
            "", imageTestResult_);
      }
    }
    return;
  }

  if (page_ == 4 && y >= 181 && y < 205 && x < 120) {
    calibrationMode_ = true;
    calibrationPoint_ = 0;
    display.showTouchCalibration(calibrationPoint_);
    return;
  }

  if (wifiKeyboardMode_) {
    if (y < 58 && x >= 250) {
      wifiKeyboardMode_ = false;
      display.drawPage(page_, wifi, files, showWifiQr, galleryPath_, filePath_);
      return;
    }
    if (y >= 87 && y < 162) {
      const uint8_t row = (y - 87) / 25;
      const char* letterRows[] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
      const char* symbolRows[] = {"1234567890", "@#$%&*-+()", "!?.,:_/;"};
      const uint8_t rowLength[] = {
          10, 9, static_cast<uint8_t>(wifiKeyboardSymbols_ ? 8 : 7)};
      const int startX =
          row == 1 ? 18 : (row == 2 && !wifiKeyboardSymbols_ ? 41 : 5);
      const int index = (x - startX) / 30;
      if (row == 2 && x >= 258 && y < 161) {
        if (!wifiKeyboardValue_.isEmpty())
          wifiKeyboardValue_.remove(wifiKeyboardValue_.length() - 1);
      } else if (x >= startX && index >= 0 && index < rowLength[row]) {
        char key = wifiKeyboardSymbols_ ? symbolRows[row][index]
                                        : letterRows[row][index];
        if (!wifiKeyboardSymbols_ && wifiKeyboardUppercase_ &&
            key >= 'a' && key <= 'z')
          key = static_cast<char>(key - 'a' + 'A');
        const size_t maxLength = wifiKeyboardPassword_ ? 63 : 32;
        if (wifiKeyboardValue_.length() < maxLength)
          wifiKeyboardValue_ += key;
      }
    } else if (y >= 163 && y < 188) {
      if (x < 68) {
        wifiKeyboardSymbols_ = !wifiKeyboardSymbols_;
      } else if (x < 127) {
        wifiKeyboardUppercase_ = !wifiKeyboardUppercase_;
      } else if (x < 205) {
        const size_t maxLength = wifiKeyboardPassword_ ? 63 : 32;
        if (wifiKeyboardValue_.length() < maxLength)
          wifiKeyboardValue_ += ' ';
      } else if (!wifiKeyboardPassword_ && !wifiKeyboardValue_.isEmpty()) {
        wifiSsid_ = wifiKeyboardValue_;
        wifiKeyboardValue_ = "";
        wifiKeyboardPassword_ = true;
        wifiKeyboardSymbols_ = false;
        wifiKeyboardUppercase_ = false;
      } else if (wifiKeyboardPassword_) {
        String error;
        if (wifi.configureStation(wifiSsid_, wifiKeyboardValue_, error)) {
          wifiKeyboardMode_ = false;
          display.drawPage(page_, wifi, files, showWifiQr, galleryPath_,
                           filePath_);
          display.drawWifiNotice("Saved. Connecting to " + wifiSsid_);
          wifiKeyboardValue_ = "";
        } else {
          display.drawWifiNotice(error);
        }
      }
    }
    if (wifiKeyboardMode_)
      display.drawWifiKeyboard(wifiKeyboardPassword_ ? "Password" : "Network",
                               wifiKeyboardValue_, wifiKeyboardSymbols_,
                               wifiKeyboardUppercase_);
    return;
  }

  if (deleteConfirm_) {
    deleteConfirm_ = false;
    if (y >= 138 && y < 165 && x >= 170) {
      String error;
      if (!files.removePath(pendingDeletePath_, error))
        Serial.printf("[Files] Delete failed: %s\n", error.c_str());
      if (deleteWasPhoto_) display.closePhoto();
      if (deleteFromGallery_)
      {
        refreshGallery(files);
        display.setGalleryOffset(galleryOffset_);
      } else {
        refreshFiles(files);
        display.setFileOffset(fileOffset_);
      }
    } else if (deleteWasPhoto_) {
      display.showPhoto(files, pendingDeletePath_);
    }
    pendingDeletePath_ = "";
    deleteWasPhoto_ = false;
    display.drawPage(page_, wifi, files, showWifiQr, galleryPath_, filePath_);
    return;
  }

  if (display.photoVisible()) {
    if (y < 32 && x < 75) {
      display.closePhoto();
      refreshGallery(files);
      display.drawPage(page_, wifi, files, showWifiQr, galleryPath_, filePath_);
    } else if (y >= 178 && y < 209 && x >= 95 && x < 225) {
      deleteHoldPending_ = true;
      deleteHoldSince_ = millis();
      deleteFromGallery_ = true;
      deleteWasPhoto_ = true;
      pendingDeletePath_ = photoPath_;
      display.drawGalleryMessage("Hold DELETE for 0.8 seconds");
    } else if (y >= 178 && y < 209 && (x < 95 || x >= 225)) {
      if (galleryItems_.empty()) return;
      for (size_t attempts = 0; attempts < galleryItems_.size(); ++attempts) {
        photoIndex_ = x < 95
                          ? (photoIndex_ + galleryItems_.size() - 1) %
                                galleryItems_.size()
                          : (photoIndex_ + 1) % galleryItems_.size();
        if (!galleryDirectories_[photoIndex_]) {
          photoPath_ = galleryItems_[photoIndex_];
          if (display.showPhoto(files, photoPath_)) break;
        }
      }
    }
    return;
  }

  if (y >= 209) {
    const uint8_t nextPage = min<uint8_t>(4, x / 64);
    if (page_ != nextPage) {
      albumInputMode_ = false;
      wifiKeyboardMode_ = false;
      page_ = nextPage;
      if (page_ == 2) {
        String error;
        if (files.isMounted(galleryPath_.startsWith("/external")))
          files.makeDirectory(galleryPath_, error);
        refreshGallery(files);
        display.setGalleryOffset(galleryOffset_);
      } else if (page_ == 1) {
        refreshFiles(files);
        display.setFileOffset(fileOffset_);
      }
      display.drawPage(page_, wifi, files, showWifiQr, galleryPath_, filePath_);
    }
    return;
  }

  if (page_ == 0 && y > 110 && x > 160) {
    page_ = 4;
    display.drawPage(page_, wifi, files, showWifiQr, galleryPath_, filePath_);
    return;
  }

  if (page_ == 1) {
    if (y >= 70 && y < 94) {
      if (x < 72) {
        filePath_ = parentPath(filePath_);
        fileOffset_ = 0;
      } else if (x < 120) {
        fileOffset_ = fileOffset_ >= 5 ? fileOffset_ - 5 : 0;
      } else if (x < 168) {
        if (fileOffset_ + 5 < fileCount_) fileOffset_ += 5;
      } else if (x >= 235) {
        if (filePath_.startsWith("/external")) filePath_ = "/";
        else filePath_ = "/external";
        fileOffset_ = 0;
      }
      refreshFiles(files);
      display.setFileOffset(fileOffset_);
      display.drawPage(page_, wifi, files, showWifiQr, galleryPath_, filePath_);
      return;
    }
    if (y < 98 || y >= 204) return;
    bool ok = false;
    String error;
    const String json = files.listJson(filePath_, ok, error);
    if (!ok) return;
    DynamicJsonDocument doc(8192);
    if (deserializeJson(doc, json)) return;
    const size_t index = fileOffset_ + (y - 98) / 20;
    if (index >= static_cast<size_t>(fileOffset_) + 5) return;
    JsonArray entries = doc["entries"].as<JsonArray>();
    if (index >= entries.size()) return;
    JsonObject item = entries[index];
    const String itemPath = item["path"] | "";
    if (x >= 265) {
      deleteHoldPending_ = true;
      deleteHoldSince_ = millis();
      deleteFromGallery_ = false;
      deleteWasPhoto_ = false;
      pendingDeletePath_ = itemPath;
      display.drawGalleryMessage("Hold DELETE for 0.8 seconds");
    } else if (item["directory"].as<bool>()) {
      filePath_ = itemPath;
      fileOffset_ = 0;
      refreshFiles(files);
      display.setFileOffset(fileOffset_);
      display.drawPage(page_, wifi, files, showWifiQr, galleryPath_, filePath_);
    }
    return;
  }

  if (page_ == 2) {
    if (albumInputMode_) {
      if (y >= 95 && y < 179) {
        const uint8_t row = (y - 95) / 28;
        const uint8_t column = (x - 6) / 31;
        const char* keys[] = {"ABCDEFGHIJ", "KLMNOPQRST", "UVWXYZ-_"};
        const String keyRow = keys[row];
        if (column < keyRow.length() && albumName_.length() < 24)
          albumName_ += keyRow[column];
      } else if (y >= 181 && y < 205) {
        if (x < 95) {
          if (!albumName_.isEmpty()) albumName_.remove(albumName_.length() - 1);
        } else if (x < 218) {
          if (!albumName_.isEmpty()) {
            String error;
            const String path = joinPath(galleryPath_, albumName_);
            if (files.makeDirectory(path, error)) {
              albumInputMode_ = false;
              albumName_ = "";
              refreshGallery(files);
            } else {
              Serial.printf("[Gallery] Create album failed: %s\n",
                            error.c_str());
            }
          }
        } else {
          albumInputMode_ = false;
          albumName_ = "";
        }
      }
      display.drawAlbumKeyboard(albumName_);
      return;
    }

    if (y >= 69 && y < 94) {
      if (x < 68) {
        if (galleryPath_ != "/Albums" && galleryPath_ != "/external/Albums") {
          galleryPath_ = parentPath(galleryPath_);
          galleryOffset_ = 0;
        }
      } else if (x < 135) {
        galleryPath_ = galleryPath_.startsWith("/external") ? "/Albums"
                                                              : "/external/Albums";
        galleryOffset_ = 0;
        String error;
        if (!galleryPath_.startsWith("/external") || files.isMounted(true))
          files.makeDirectory(galleryPath_, error);
      } else if (x < 172) {
        galleryOffset_ = galleryOffset_ >= 5 ? galleryOffset_ - 5 : 0;
      } else if (x < 209) {
        if (galleryOffset_ + 5 < galleryCount_) galleryOffset_ += 5;
      } else if (x < 250) {
        display.invalidateThumbnails();
        refreshGallery(files);
      } else {
        albumInputMode_ = true;
        albumName_ = "";
        display.drawAlbumKeyboard(albumName_);
        return;
      }
      refreshGallery(files);
      display.setGalleryOffset(galleryOffset_);
      display.drawPage(page_, wifi, files, showWifiQr, galleryPath_, filePath_);
      return;
    }

    if (y < 103 || y >= 203) return;
    const size_t index = galleryOffset_ + (y - 103) / 20;
    if (index >= galleryItems_.size()) return;
    const String selected = galleryItems_[index];
    String error;
    if (x >= 270) {
      deleteHoldPending_ = true;
      deleteHoldSince_ = millis();
      deleteFromGallery_ = true;
      pendingDeletePath_ = selected;
      display.drawGalleryMessage("Hold DELETE for 0.8 seconds");
    } else if (galleryDirectories_[index]) {
      galleryPath_ = selected;
      galleryOffset_ = 0;
      refreshGallery(files);
      display.setGalleryOffset(galleryOffset_);
    } else {
      photoIndex_ = index;
      photoPath_ = selected;
      if (!display.showPhoto(files, photoPath_)) {
        Serial.printf("[Gallery] Unable to decode image: %s\n",
                      selected.c_str());
        display.drawPage(page_, wifi, files, showWifiQr, galleryPath_,
                         filePath_);
      }
    }
    if (!display.photoVisible())
      display.drawPage(page_, wifi, files, showWifiQr, galleryPath_, filePath_);
    return;
  }

  if (page_ == 4 && y >= 86 && y < 180 && x >= 230) {
    showWifiQr = !showWifiQr;
    display.drawPage(page_, wifi, files, showWifiQr, galleryPath_, filePath_);
  } else if (page_ == 4 && y >= 181 && y < 205 && x >= 120 && x < 230) {
    imageTestMode_ = true;
    refreshGallery(files);
    imageTestIndex_ = 0;
    while (imageTestIndex_ < galleryItems_.size() &&
           (galleryDirectories_[imageTestIndex_] ||
            !isImageFile(galleryItems_[imageTestIndex_]))) {
      ++imageTestIndex_;
    }
    imageTestResult_ =
        imageTestIndex_ < galleryItems_.size()
            ? "Ready. LOAD IMAGE to run a decoder test."
            : "No supported images in " + galleryPath_;
    display.drawImageTestPage(
        files.isMounted(galleryPath_.startsWith("/external")), ESP.getFreeHeap(),
        heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
        imageTestIndex_ < galleryItems_.size()
            ? galleryItems_[imageTestIndex_]
            : String(),
        imageTestResult_);
  } else if (page_ == 4 && y >= 68 && y < 92 && x < 118) {
    wifiSsid_ = wifi.stationSsid();
    wifiKeyboardValue_ = wifiSsid_;
    wifiKeyboardPassword_ = false;
    wifiKeyboardSymbols_ = false;
    wifiKeyboardUppercase_ = false;
    wifiKeyboardMode_ = true;
    display.drawWifiKeyboard("Network", wifiKeyboardValue_,
                             wifiKeyboardSymbols_, wifiKeyboardUppercase_);
  } else if (page_ == 4 && y >= 150 && y < 180 && x < 230) {
    const int next = display.brightness() + (x < 115 ? -10 : 10);
    display.setBrightness(constrain(next, 5, 100));
    Preferences preferences;
    preferences.begin("filehub", false);
    preferences.putUChar("brightness", display.brightness());
    preferences.end();
    display.drawPage(page_, wifi, files, showWifiQr, galleryPath_, filePath_);
  }
}

uint8_t TouchManager::page() const { return page_; }
String TouchManager::galleryPath() const { return galleryPath_; }
String TouchManager::filePath() const { return filePath_; }
bool TouchManager::wifiQr() const { return showWifiQr; }
bool TouchManager::overlayVisible() const {
  return albumInputMode_ || deleteConfirm_ || wifiKeyboardMode_ ||
         calibrationMode_ || imageTestMode_;
}
