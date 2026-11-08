#include "display_manager.h"

#include <ArduinoJson.h>
#include <PNGdec.h>
#include <TJpg_Decoder.h>
#include <qrcode.h>
#include "cyd_config.h"
#include "transfer_state.h"
#include <esp_heap_caps.h>
#include <new>
#include <time.h>

namespace {
constexpr uint16_t kBackground = 0x1082;
constexpr uint16_t kPanel = 0x2125;
constexpr uint16_t kAccent = 0x36DF;
constexpr uint16_t kGreen = 0x2FE5;
constexpr uint16_t kOrange = 0xFD20;
constexpr uint16_t kMuted = 0x9CF3;
TFT_eSPI* photoTft = nullptr;
PNG pngDecoder;
FS* pngVolume = nullptr;
TFT_eSPI* pngTft = nullptr;
int pngOriginX = 0;
int pngOriginY = 0;
int pngScale = 1;
uint16_t pngLine[320];
uint16_t pngScaledLine[320];
uint16_t* thumbnailCapture = nullptr;
int pngDrawLine(PNGDRAW* draw) {
  if (pngTft == nullptr || draw->iWidth > 320 ||
      draw->y % pngScale != 0) return 1;
  pngDecoder.getLineAsRGB565(draw, pngLine, PNG_RGB565_LITTLE_ENDIAN, 0);
  const int width = (draw->iWidth + pngScale - 1) / pngScale;
  for (int i = 0; i < width; ++i)
    pngScaledLine[i] = pngLine[min(i * pngScale, draw->iWidth - 1)];
  pngTft->pushImage(pngOriginX, pngOriginY + draw->y / pngScale,
                    width, 1, pngScaledLine);
  return 1;
}

void* pngOpen(const char* filename, int32_t* size) {
  if (pngVolume == nullptr) return nullptr;
  File* file = new (std::nothrow) File(pngVolume->open(filename, FILE_READ));
  if (file == nullptr || !*file) {
    delete file;
    return nullptr;
  }
  *size = file->size();
  return file;
}

void pngClose(void* handle) {
  File* file = static_cast<File*>(handle);
  if (file != nullptr) {
    file->close();
    delete file;
  }
}

int32_t pngRead(PNGFILE* file, uint8_t* buffer, int32_t length) {
  return file == nullptr || file->fHandle == nullptr
             ? 0
             : static_cast<File*>(file->fHandle)->read(buffer, length);
}

int32_t pngSeek(PNGFILE* file, int32_t position) {
  if (file == nullptr || file->fHandle == nullptr) return -1;
  const bool ok = static_cast<File*>(file->fHandle)->seek(position);
  return ok ? position : -1;
}

uint16_t readLe16(const uint8_t* data) {
  return static_cast<uint16_t>(data[0] | (data[1] << 8));
}

uint32_t readLe32(const uint8_t* data) {
  return static_cast<uint32_t>(data[0]) |
         (static_cast<uint32_t>(data[1]) << 8) |
         (static_cast<uint32_t>(data[2]) << 16) |
         (static_cast<uint32_t>(data[3]) << 24);
}

bool drawBmp(File& file, TFT_eSPI& tft, int originX, int originY) {
  uint8_t header[54];
  if (file.size() < sizeof(header) || file.read(header, sizeof(header)) !=
                                          sizeof(header) ||
      header[0] != 'B' || header[1] != 'M' ||
      readLe32(header + 14) < 40 || readLe16(header + 26) != 1 ||
      readLe16(header + 28) != 24 || readLe32(header + 30) != 0)
    return false;
  const uint32_t pixelOffset = readLe32(header + 10);
  const int32_t width = static_cast<int32_t>(readLe32(header + 18));
  const int32_t signedHeight = static_cast<int32_t>(readLe32(header + 22));
  const int32_t height = abs(signedHeight);
  if (width <= 0 || width > 4096 || height <= 0 || height > 4096 ||
      pixelOffset >= file.size())
    return false;
  const int scale = max(1, max((width + 319) / 320, (height + 145) / 146));
  const int outWidth = (width + scale - 1) / scale;
  const int outHeight = (height + scale - 1) / scale;
  const int x = originX + max(0, (320 - outWidth) / 2);
  const int y = originY + max(0, (146 - outHeight) / 2);
  const uint32_t rowBytes = (static_cast<uint32_t>(width) * 3 + 3) & ~3UL;
  uint16_t pixels[320];
  uint8_t bgr[3];
  for (int32_t outY = 0; outY < outHeight; ++outY) {
    const int32_t sourceY = outY * scale;
    const int32_t bmpY = signedHeight > 0 ? height - 1 - sourceY : sourceY;
    if (!file.seek(pixelOffset + static_cast<uint32_t>(bmpY) * rowBytes))
      return false;
    int outX = 0;
    for (int32_t sourceX = 0; sourceX < width; ++sourceX) {
      if (file.read(bgr, sizeof(bgr)) != sizeof(bgr)) return false;
      if (sourceX % scale == 0 && outX < outWidth) {
        pixels[outX++] = tft.color565(bgr[2], bgr[1], bgr[0]);
      }
    }
    if (y + outY >= 31 && y + outY < 178 && x < 320)
      tft.pushImage(x, y + outY, min(outX, 320 - x), 1, pixels);
  }
  return true;
}

bool photoOutput(int16_t x, int16_t y, uint16_t width, uint16_t height,
                 uint16_t* pixels) {
  if (thumbnailCapture != nullptr) {
    for (uint16_t row = 0; row < height && y + row < 18; ++row) {
      for (uint16_t column = 0; column < width && x + column < 30; ++column)
        thumbnailCapture[(y + row) * 30 + x + column] =
            pixels[row * width + column];
    }
    return true;
  }
  if (photoTft == nullptr) return 1;
  photoTft->pushImage(x, y, width, height, pixels);
  return 1;
}

String formatBytes(uint64_t bytes) {
  if (bytes >= 1024ULL * 1024ULL * 1024ULL)
    return String(bytes / (1024.0 * 1024.0 * 1024.0), 1) + " GB";
  if (bytes >= 1024ULL * 1024ULL)
    return String(bytes / (1024.0 * 1024.0), 1) + " MB";
  return String(bytes / 1024.0, 1) + " KB";
}
}  // namespace

void DisplayManager::begin() {
  pinMode(CydPins::kBacklight, OUTPUT);
  analogWrite(CydPins::kBacklight, 255);
  tft_.init();
  tft_.setRotation(1);
  photoTft = &tft_;
  TJpgDec.setCallback(photoOutput);
  TJpgDec.setSwapBytes(true);
  Serial.printf("[Gallery] TFT SPI host: HSPI; SD SPI host: VSPI; touch shares VSPI pins with SD\n");
  tft_.fillScreen(kBackground);
  tft_.setTextDatum(TL_DATUM);
  tft_.setTextColor(TFT_WHITE, kBackground);
  tft_.drawString("ESP32 File Hub", 18, 83, 4);
  tft_.setTextColor(kMuted, kBackground);
  tft_.drawString("Starting services...", 20, 130, 2);
}

bool DisplayManager::readTouch(uint16_t& x, uint16_t& y, uint16_t& pressure,
                               uint16_t& rawX, uint16_t& rawY) {
  pressure = tft_.getTouchRawZ();
  tft_.getTouchRaw(&rawX, &rawY);
  if (pressure < 100 || pressure > 4000) return false;
  tft_.convertRawXY(&rawX, &rawY);
  x = rawX;
  y = rawY;
  return x < 320 && y < 240;
}

void DisplayManager::setBrightness(uint8_t percent) {
  brightness_ = constrain(percent, 5, 100);
  analogWrite(CydPins::kBacklight,
              map(brightness_, 0, 100, 0, 255));
}

uint8_t DisplayManager::brightness() const { return brightness_; }

void DisplayManager::drawStatusBar(WifiManager& wifi, FileManager& files) {
  tft_.fillRect(0, 0, 320, 31, kPanel);
  tft_.setTextColor(wifi.connected() ? kGreen : kOrange, kPanel);
  tft_.drawString(wifi.connected() ? "WiFi" : "AP", 8, 8, 2);
  tft_.setTextColor(TFT_WHITE, kPanel);
  tft_.drawString(wifi.ipAddress(), 54, 8, 2);
  tft_.setTextColor(files.isMounted() ? kGreen : TFT_RED, kPanel);
  tft_.drawString(files.isMounted() ? "SD" : "SD!", 222, 8, 2);
  tft_.setTextColor(files.isMounted(true) ? kGreen : kMuted, kPanel);
  tft_.drawString(files.isMounted(true) ? "EXT" : "--", 183, 8, 1);
  struct tm now;
  if (getLocalTime(&now, 0)) {
    char timeText[6];
    strftime(timeText, sizeof(timeText), "%H:%M", &now);
    tft_.setTextColor(kMuted, kPanel);
    tft_.drawString(timeText, 274, 8, 2);
  } else {
    tft_.setTextColor(kMuted, kPanel);
    tft_.drawString("--:--", 274, 8, 2);
  }
}

void DisplayManager::drawNavigation(uint8_t page) {
  tft_.fillRect(0, 209, 320, 31, kPanel);
  const char* labels[] = {"Home", "Files", "Gallery", "Transfer", "Settings"};
  constexpr int width = 64;
  for (uint8_t i = 0; i < 5; ++i) {
    const uint16_t color = i == page ? kAccent : kMuted;
    const int x = i * width;
    tft_.fillRoundRect(x + 2, 213, width - 4, 23, 7,
                       i == page ? 0x318A : kPanel);
    tft_.setTextDatum(MC_DATUM);
    tft_.setTextColor(color, i == page ? 0x318A : kPanel);
    tft_.drawString(labels[i], x + width / 2, 224, 1);
  }
  tft_.setTextDatum(TL_DATUM);
}

void DisplayManager::drawHome(WifiManager& wifi, FileManager& files) {
  const uint64_t total = files.totalBytes();
  const uint64_t used = files.usedBytes();
  tft_.setTextColor(TFT_WHITE, kBackground);
  tft_.drawString("ESP32 File Hub", 13, 43, 4);
  tft_.setTextColor(kMuted, kBackground);
  tft_.drawString(wifi.modeName() + "  " + wifi.ssid(), 15, 79, 2);
  tft_.fillRoundRect(12, 107, 143, 82, 10, kPanel);
  tft_.fillRoundRect(165, 107, 143, 82, 10, kPanel);
  tft_.setTextColor(kMuted, kPanel);
  tft_.drawString("SD STORAGE", 23, 117, 2);
  tft_.setTextColor(TFT_WHITE, kPanel);
  tft_.drawString(files.isMounted() ? formatBytes(used) : "Not mounted",
                  23, 142, 4);
  tft_.setTextColor(kMuted, kPanel);
  tft_.drawString("of " + formatBytes(total), 23, 172, 2);
  tft_.setTextColor(kMuted, kPanel);
  tft_.drawString("NETWORK", 177, 117, 2);
  tft_.setTextColor(TFT_WHITE, kPanel);
  tft_.drawString(wifi.ipAddress(), 177, 145, 2);
  tft_.setTextColor(wifi.connected() ? kGreen : kOrange, kPanel);
  tft_.drawString(wifi.connected() ? "Connected" : "Access point", 177, 173, 2);
  tft_.setTextColor(files.isMounted(true) ? kGreen : kMuted, kPanel);
  tft_.drawString(files.isMounted(true)
                      ? "EXT " + formatBytes(files.usedBytes(true))
                      : "No external SD",
                  177, 188, 1);
}

void DisplayManager::drawFiles(FileManager& files, const String& path) {
  bool ok = false;
  String error;
  const String json = files.listJson(path, ok, error);
  tft_.setTextColor(TFT_WHITE, kBackground);
  tft_.drawString("Files", 14, 40, 4);
  tft_.setTextColor(kMuted, kBackground);
  tft_.drawString(path, 91, 48, 2);
  tft_.fillRoundRect(10, 70, 60, 22, 5, kPanel);
  tft_.fillRoundRect(76, 70, 43, 22, 5, kPanel);
  tft_.fillRoundRect(123, 70, 43, 22, 5, kPanel);
  tft_.fillRoundRect(240, 70, 70, 22, 5, kPanel);
  tft_.setTextColor(kAccent, kPanel);
  tft_.drawString("UP", 27, 74, 2);
  tft_.drawString("<", 93, 73, 2);
  tft_.drawString(">", 140, 73, 2);
  tft_.drawString(path.startsWith("/external") ? "INTERNAL" : "EXTERNAL",
                  246, 75, 1);
  if (!ok) {
    tft_.setTextColor(TFT_RED, kBackground);
    tft_.drawString(error, 16, 105, 2);
    return;
  }
  DynamicJsonDocument doc(8192);
  const DeserializationError parseError = deserializeJson(doc, json);
  const uint16_t count = parseError ? 0 : doc["count"].as<uint16_t>();
  const uint16_t folders = parseError ? 0 : doc["folders"].as<uint16_t>();
  tft_.setTextColor(kMuted, kBackground);
  const uint16_t pages = (count + 4) / 5;
  const uint16_t currentPage = pages ? fileOffset_ / 5 + 1 : 0;
  tft_.drawString(String(currentPage) + "/" + String(pages), 173, 75, 1);
  tft_.fillRoundRect(10, 98, 300, 106, 8, kPanel);
  if (!parseError) {
    JsonArray entries = doc["entries"].as<JsonArray>();
    for (uint8_t i = 0; i < 5; ++i) {
      const uint16_t entryIndex = fileOffset_ + i;
      if (entryIndex >= entries.size()) break;
      JsonObject item = entries[entryIndex];
      String label = item["directory"].as<bool>() ? "[DIR] " : "      ";
      label += String(item["name"] | "");
      if (label.length() > 26) label = label.substring(0, 23) + "...";
      tft_.setTextColor(item["directory"].as<bool>() ? kAccent : TFT_WHITE,
                        kPanel);
      tft_.drawString(label, 16, 102 + i * 20, 2);
      tft_.setTextColor(TFT_RED, kPanel);
      tft_.drawString("DEL", 276, 102 + i * 20, 1);
    }
  }
}

void DisplayManager::drawGallery(FileManager& files, const String& path) {
  bool ok = false;
  String error;
  const String json = files.listJson(path, ok, error);
  tft_.setTextColor(TFT_WHITE, kBackground);
  tft_.drawString("Gallery", 14, 40, 4);
  tft_.setTextColor(kMuted, kBackground);
  tft_.drawString(path, 115, 48, 2);
  const int buttonX[] = {8, 70, 137, 174, 211, 252};
  const int buttonW[] = {58, 62, 33, 33, 37, 60};
  for (uint8_t i = 0; i < 6; ++i)
    tft_.fillRoundRect(buttonX[i], 69, buttonW[i], 23, 5, kPanel);
  tft_.setTextColor(kAccent, kPanel);
  tft_.drawString("UP", 23, 73, 2);
  tft_.drawString(path.startsWith("/external") ? "EXT" : "INT", 84, 74, 2);
  tft_.drawString("<", 150, 74, 2);
  tft_.drawString(">", 187, 74, 2);
  tft_.drawString("SCAN", 214, 75, 1);
  tft_.drawString("+ALB", 260, 75, 1);
  if (!ok) {
    tft_.setTextColor(TFT_RED, kBackground);
    tft_.drawString(error, 15, 112, 2);
    return;
  }
  DynamicJsonDocument doc(8192);
  const DeserializationError parseError = deserializeJson(doc, json);
  if (parseError) {
    Serial.printf("[Gallery] Folder listing parse failed: %s\n",
                  parseError.c_str());
    tft_.setTextColor(TFT_RED, kBackground);
    tft_.drawString("Gallery listing could not be read", 14, 112, 2);
    return;
  }
  JsonArray entries = doc["entries"].as<JsonArray>();
  uint16_t imageCount = 0;
  uint16_t albumCount = 0;
  for (JsonObject item : entries) {
    String name = item["name"] | "";
    name.toLowerCase();
    if (item["directory"].as<bool>()) ++albumCount;
    else if (name.endsWith(".jpg") || name.endsWith(".jpeg") ||
             name.endsWith(".png") || name.endsWith(".bmp"))
      ++imageCount;
  }
  tft_.setTextColor(kMuted, kBackground);
  tft_.drawString("Images Found: " + String(imageCount) +
                      "  Albums Found: " + String(albumCount),
                  10, 94, 1);
  uint16_t filteredIndex = 0;
  uint8_t row = 0;
  for (JsonObject item : entries) {
    const String name = item["name"] | "";
    String lower = name;
    lower.toLowerCase();
    const bool directory = item["directory"].as<bool>();
    const bool jpeg = lower.endsWith(".jpg") || lower.endsWith(".jpeg");
    const bool image = jpeg || lower.endsWith(".png") || lower.endsWith(".bmp");
    if (!directory && !image) continue;
    if (filteredIndex++ < galleryOffset_) continue;
    if (row >= 5) break;
    const int y = 104 + row * 20;
    tft_.fillRoundRect(8, y, 304, 19, 4, kPanel);
    if (jpeg) {
      Thumbnail& thumbnail = thumbnails_[row];
      if (thumbnail.path != String(item["path"] | "")) {
        thumbnail.path = item["path"] | "";
        thumbnail.valid = false;
        memset(thumbnail.pixels, 0, sizeof(thumbnail.pixels));
        const uint32_t freeHeap = ESP.getFreeHeap();
        const uint32_t largestBlock =
            heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
        FS* volume = nullptr;
        String physicalPath;
        String readError;
        Serial.printf("[Gallery] Thumbnail path=%s freeHeap=%u largestBlock=%u\n",
                      thumbnail.path.c_str(), freeHeap, largestBlock);
        if (largestBlock < 8192) {
          readError = "Insufficient largest free heap block";
        } else if (files.beginFileRead(thumbnail.path, volume, physicalPath,
                                       readError)) {
          uint16_t width = 0;
          uint16_t height = 0;
          const bool exists = volume != nullptr && volume->exists(physicalPath);
          const JRESULT sizeResult =
              exists ? TJpgDec.getFsJpgSize(&width, &height, physicalPath,
                                            *volume)
                     : JDR_INP;
          if (exists && sizeResult == JDR_OK && width > 0 && height > 0) {
            thumbnailCapture = thumbnail.pixels;
            TJpgDec.setJpgScale(8);
            const JRESULT decodeResult =
                TJpgDec.drawFsJpg(0, 0, physicalPath, *volume);
            TJpgDec.setJpgScale(1);
            thumbnailCapture = nullptr;
            thumbnail.valid = decodeResult == JDR_OK;
            if (!thumbnail.valid)
              Serial.printf("[Gallery] Thumbnail JPEG decode result=%d path=%s\n",
                            decodeResult, thumbnail.path.c_str());
          } else {
            Serial.printf("[Gallery] Thumbnail file=%s exists=%s sizeResult=%d dimensions=%ux%u\n",
                          physicalPath.c_str(), exists ? "yes" : "no",
                          sizeResult, width, height);
          }
          if (!thumbnail.valid)
            Serial.printf("[Gallery] Thumbnail Decode Failed: %s\n",
                          thumbnail.path.c_str());
          files.endFileRead();
        } else {
          Serial.printf("[Gallery] Thumbnail unavailable: %s (%s)\n",
                        thumbnail.path.c_str(), readError.c_str());
        }
      }
      if (thumbnail.valid) {
        tft_.pushImage(12, y + 1, 30, 18, thumbnail.pixels);
      } else {
        tft_.fillRect(12, y + 2, 30, 15, 0x528A);
        tft_.setTextColor(TFT_WHITE, 0x528A);
        tft_.drawCentreString("JPG", 27, y + 6, 1);
      }
    } else {
      tft_.fillRect(12, y + 2, 30, 15, 0x528A);
      tft_.setTextColor(TFT_WHITE, 0x528A);
      tft_.drawCentreString(directory ? "DIR" : (lower.endsWith(".png") ? "PNG" : "BMP"),
                            27, y + 6, 1);
    }
    tft_.setTextColor(directory ? kAccent : TFT_WHITE, kPanel);
    String label = (directory ? "[ALBUM] " : "") + name;
    if (label.length() > 31) label = label.substring(0, 28) + "...";
    tft_.drawString(label, 48, y + 3, 1);
    tft_.setTextColor(TFT_RED, kPanel);
    tft_.drawString("DEL", 279, y + 3, 1);
    ++row;
  }
  const uint16_t pages = (imageCount + albumCount + 4) / 5;
  tft_.setTextColor(kMuted, kBackground);
  tft_.drawString(String(pages ? galleryOffset_ / 5 + 1 : 0) + "/" +
                      String(pages),
                  293, 48, 1);
  if (row == 0) {
    tft_.setTextColor(kMuted, kBackground);
    tft_.drawString("No albums or supported images", 14, 119, 2);
  }
}

void DisplayManager::invalidateThumbnails() {
  for (Thumbnail& thumbnail : thumbnails_) {
    thumbnail.path = "";
    thumbnail.valid = false;
  }
}

bool DisplayManager::showPhoto(FileManager& files, const String& path,
                               bool testMode) {
  photoVisible_ = false;
  currentPhoto_ = "";
  photoTestMode_ = testMode;
  const uint32_t freeHeap = ESP.getFreeHeap();
  const uint32_t largestBlock =
      heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  Serial.printf("[Gallery] Loading: %s freeHeap=%u largestBlock=%u\n",
                path.c_str(), freeHeap, largestBlock);
  String physicalPath;
  String error;
  FS* volume = nullptr;
  if (!files.beginFileRead(path, volume, physicalPath, error)) {
    Serial.printf("[Gallery] Decode Failed: %s (%s)\n", path.c_str(),
                  error.c_str());
    drawPhotoError(path, error);
    return false;
  }
  bool decoded = false;
  int decodeResult = -1;
  String lowerPath = path;
  lowerPath.toLowerCase();
  if (volume == nullptr || !volume->exists(physicalPath)) {
    error = "Image file is missing";
  } else {
    File metadata = volume->open(physicalPath, FILE_READ);
    if (!metadata || metadata.isDirectory()) {
      error = "Image file cannot be opened";
      if (metadata) metadata.close();
    } else {
      const uint32_t fileSize = metadata.size();
      metadata.close();
      Serial.printf("[Gallery] Path verified logical=%s physical=%s size=%u\n",
                    path.c_str(), physicalPath.c_str(),
                    static_cast<unsigned>(fileSize));
    }
    if (!error.isEmpty()) {
      files.endFileRead();
      Serial.printf("[Gallery] File validation failed: %s (%s)\n",
                    path.c_str(), error.c_str());
      drawPhotoError(path, error);
      return false;
    }
    tft_.fillScreen(TFT_BLACK);
    if (lowerPath.endsWith(".jpg") || lowerPath.endsWith(".jpeg")) {
      uint16_t imageWidth = 0;
      uint16_t imageHeight = 0;
      if (largestBlock < 8192) {
        error = "Insufficient free heap for JPEG decoder";
      } else {
        Serial.printf("[Gallery] Rendering: %s\n", path.c_str());
        const JRESULT sizeResult =
            TJpgDec.getFsJpgSize(&imageWidth, &imageHeight, physicalPath,
                                 *volume);
        decodeResult = sizeResult;
        if (sizeResult != JDR_OK || imageWidth == 0 || imageHeight == 0) {
          error = "JPEG header failed";
          Serial.printf("[Gallery] JPEG header result=%d dimensions=%ux%u\n",
                        sizeResult, imageWidth, imageHeight);
        } else {
          uint8_t scale = 1;
          while (scale < 8 &&
                 (imageWidth / scale > 320 || imageHeight / scale > 146))
            scale *= 2;
          const int scaledWidth = imageWidth / scale;
          const int scaledHeight = imageHeight / scale;
          const int x = max(0, (320 - scaledWidth) / 2);
          const int y = 32 + max(0, (146 - scaledHeight) / 2);
          TJpgDec.setJpgScale(scale);
          decodeResult = TJpgDec.drawFsJpg(x, y, physicalPath, *volume);
          decoded = decodeResult == JDR_OK;
          TJpgDec.setJpgScale(1);
          if (!decoded) {
            error = decodeResult == JDR_INTR
                        ? "JPEG drawing callback aborted"
                        : "JPEG corrupt or unsupported";
          }
        }
      }
    } else if (lowerPath.endsWith(".png")) {
      Serial.printf("[Gallery] Rendering: %s\n", path.c_str());
      if (freeHeap < 48 * 1024) {
        error = "Insufficient free heap for PNG decoder";
      } else {
        pngVolume = volume;
        pngTft = &tft_;
        pngScale = 1;
        const int openResult =
            pngDecoder.open(physicalPath.c_str(), pngOpen, pngClose, pngRead,
                            pngSeek, pngDrawLine);
        if (openResult == PNG_SUCCESS && pngDecoder.getWidth() > 0 &&
            pngDecoder.getHeight() > 0 && pngDecoder.getWidth() <= 320 &&
            !pngDecoder.isInterlaced()) {
          pngScale = max(1, max((pngDecoder.getWidth() + 319) / 320,
                                (pngDecoder.getHeight() + 145) / 146));
          const int scaledWidth =
              (pngDecoder.getWidth() + pngScale - 1) / pngScale;
          const int scaledHeight =
              (pngDecoder.getHeight() + pngScale - 1) / pngScale;
          pngOriginX = max(0, (320 - scaledWidth) / 2);
          pngOriginY = 32 + max(0, (146 - scaledHeight) / 2);
          decodeResult = pngDecoder.decode(nullptr, 0);
          decoded = decodeResult == PNG_SUCCESS;
          if (!decoded) error = "PNG decode failed";
        } else {
          decodeResult = openResult;
          error = pngDecoder.isInterlaced()
                      ? "Interlaced PNG is unsupported"
                      : "PNG is corrupt or wider than 320 pixels";
        }
        Serial.printf("[Gallery] PNG open=%d decode=%d error=%d dimensions=%dx%d interlaced=%d\n",
                      openResult, decodeResult, pngDecoder.getLastError(),
                      pngDecoder.getWidth(), pngDecoder.getHeight(),
                      pngDecoder.isInterlaced());
        pngDecoder.close();
        pngVolume = nullptr;
        pngTft = nullptr;
      }
    } else if (lowerPath.endsWith(".bmp")) {
      Serial.printf("[Gallery] Rendering: %s\n", path.c_str());
      File image = volume->open(physicalPath, FILE_READ);
      if (image && !image.isDirectory()) {
        decoded = drawBmp(image, tft_, 0, 32);
        image.close();
      }
      if (!decoded) error = "BMP unsupported or corrupt (24-bit BMP only)";
    } else {
      error = "Supported formats: JPG, PNG, BMP";
    }
  }
  files.endFileRead();
  if (!decoded) {
    Serial.printf("[Gallery] Decode Failed: %s (%s) result=%d freeHeap=%u largestBlock=%u\n",
                  path.c_str(), error.c_str(), decodeResult, ESP.getFreeHeap(),
                  heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    drawPhotoError(path, error);
    return false;
  }
  Serial.printf("[Gallery] Decode Success: %s decoderResult=%d freeHeap=%u\n",
                path.c_str(), decodeResult, ESP.getFreeHeap());
  currentPhoto_ = path;
  photoVisible_ = true;
  drawPhotoControls();
  return true;
}

void DisplayManager::drawPhotoError(const String& path, const String& message) {
  tft_.fillScreen(kBackground);
  tft_.fillRect(0, 0, 320, 30, kPanel);
  tft_.setTextColor(kAccent, kPanel);
  tft_.drawString("BACK", 10, 7, 2);
  String name = path.substring(path.lastIndexOf('/') + 1);
  if (name.length() > 23) name = name.substring(0, 20) + "...";
  tft_.setTextColor(TFT_WHITE, kPanel);
  tft_.drawString(name, 70, 8, 1);
  tft_.setTextColor(TFT_RED, kBackground);
  tft_.drawCentreString("Image Load Failed", 160, 88, 4);
  tft_.setTextColor(kMuted, kBackground);
  String reason = message;
  if (reason.length() > 40) reason = reason.substring(0, 37) + "...";
  tft_.drawCentreString(reason, 160, 127, 2);
  photoVisible_ = true;
  currentPhoto_ = path;
  drawPhotoControls();
}

void DisplayManager::drawGalleryMessage(const String& message) {
  tft_.fillRect(8, 185, 304, 20, kBackground);
  tft_.setTextColor(kOrange, kBackground);
  tft_.drawCentreString(message, 160, 188, 1);
}

void DisplayManager::showTouchCalibration(uint8_t point) {
  static const int points[4][2] = {{24, 42}, {296, 42}, {296, 188}, {24, 188}};
  tft_.fillScreen(kBackground);
  tft_.setTextColor(TFT_WHITE, kBackground);
  tft_.drawCentreString("Touch calibration", 160, 35, 2);
  tft_.setTextColor(kMuted, kBackground);
  tft_.drawCentreString("Tap the crosshair", 160, 58, 2);
  if (point >= 4) {
    tft_.drawCentreString("Saving calibration...", 160, 110, 2);
    return;
  }
  const int x = points[point][0];
  const int y = points[point][1];
  tft_.drawLine(x - 10, y, x + 10, y, TFT_YELLOW);
  tft_.drawLine(x, y - 10, x, y + 10, TFT_YELLOW);
  tft_.drawCircle(x, y, 4, TFT_WHITE);
}

void DisplayManager::finishTouchCalibration() {
  tft_.fillScreen(kBackground);
  tft_.setTextColor(kGreen, kBackground);
  tft_.drawCentreString("Touch calibration saved", 160, 96, 2);
  tft_.setTextColor(kMuted, kBackground);
  tft_.drawCentreString("Returning to Settings", 160, 123, 2);
}

void DisplayManager::drawPhotoControls() {
  tft_.fillRect(0, 0, 320, 30, kPanel);
  tft_.setTextColor(kAccent, kPanel);
  tft_.drawString("BACK", 10, 7, 2);
  String name = currentPhoto_.substring(currentPhoto_.lastIndexOf('/') + 1);
  if (name.length() > 23) name = name.substring(0, 20) + "...";
  tft_.setTextColor(TFT_WHITE, kPanel);
  tft_.drawString(name, 70, 8, 1);
  tft_.fillRect(0, 178, 320, 31, kPanel);
  tft_.setTextColor(kAccent, kPanel);
  tft_.drawString("< PREV", 14, 188, 2);
  tft_.setTextColor(photoTestMode_ ? kGreen : TFT_RED, kPanel);
  tft_.drawString(photoTestMode_ ? "TEST PAGE" : "DELETE", 116, 188, 2);
  tft_.setTextColor(kAccent, kPanel);
  tft_.drawString("NEXT >", 254, 188, 2);
}

void DisplayManager::drawImageTestPage(bool sdMounted, uint32_t freeHeap,
                                       uint32_t largestBlock,
                                       const String& imagePath,
                                       const String& result) {
  photoVisible_ = false;
  photoTestMode_ = true;
  tft_.fillScreen(kBackground);
  tft_.fillRect(0, 0, 320, 31, kPanel);
  tft_.setTextColor(TFT_WHITE, kPanel);
  tft_.drawString("Standalone Image Test", 8, 8, 2);
  tft_.setTextColor(sdMounted ? kGreen : TFT_RED, kBackground);
  tft_.drawString(sdMounted ? "SD mounted" : "SD unavailable", 14, 48, 2);
  tft_.setTextColor(kMuted, kBackground);
  tft_.drawString("Free heap: " + String(freeHeap), 14, 75, 2);
  tft_.drawString("Largest block: " + String(largestBlock), 14, 101, 2);
  String visiblePath = imagePath.isEmpty() ? "No supported image found"
                                           : imagePath;
  if (visiblePath.length() > 35)
    visiblePath = "..." + visiblePath.substring(visiblePath.length() - 32);
  tft_.drawString("Test file:", 14, 130, 1);
  tft_.drawString(visiblePath, 14, 146, 1);
  String visibleResult = result;
  if (visibleResult.length() > 45)
    visibleResult = visibleResult.substring(0, 42) + "...";
  tft_.setTextColor(kOrange, kBackground);
  tft_.drawString(visibleResult, 14, 167, 1);
  tft_.fillRoundRect(14, 184, 112, 20, 5, kPanel);
  tft_.fillRoundRect(194, 184, 112, 20, 5, kGreen);
  tft_.setTextColor(TFT_WHITE, kPanel);
  tft_.drawCentreString("BACK", 70, 187, 1);
  tft_.setTextColor(TFT_BLACK, kGreen);
  tft_.drawCentreString("LOAD IMAGE", 250, 187, 1);
}

void DisplayManager::closePhoto() {
  photoVisible_ = false;
  currentPhoto_ = "";
}

bool DisplayManager::photoVisible() const { return photoVisible_; }

void DisplayManager::drawAlbumKeyboard(const String& albumName) {
  tft_.fillScreen(kBackground);
  tft_.setTextColor(TFT_WHITE, kBackground);
  tft_.drawString("New album name", 12, 38, 4);
  tft_.fillRoundRect(8, 66, 304, 24, 5, kPanel);
  tft_.setTextColor(kAccent, kPanel);
  tft_.drawString(albumName + "_", 16, 71, 2);
  const char* keys[] = {"ABCDEFGHIJ", "KLMNOPQRST", "UVWXYZ-_"};
  for (uint8_t row = 0; row < 3; ++row) {
    const String chars = keys[row];
    for (uint8_t col = 0; col < chars.length(); ++col) {
      const int x = 6 + col * 31;
      const int y = 95 + row * 28;
      tft_.fillRoundRect(x, y, 28, 24, 4, kPanel);
      tft_.setTextDatum(MC_DATUM);
      tft_.setTextColor(TFT_WHITE, kPanel);
      tft_.drawString(String(chars[col]), x + 14, y + 12, 2);
    }
  }
  tft_.setTextDatum(TL_DATUM);
  tft_.fillRoundRect(8, 181, 84, 23, 5, kPanel);
  tft_.fillRoundRect(99, 181, 115, 23, 5, kGreen);
  tft_.fillRoundRect(221, 181, 91, 23, 5, kPanel);
  tft_.setTextColor(TFT_WHITE, kPanel);
  tft_.drawString("DELETE", 19, 185, 2);
  tft_.setTextColor(TFT_BLACK, kGreen);
  tft_.drawString("CREATE", 124, 185, 2);
  tft_.setTextColor(TFT_WHITE, kPanel);
  tft_.drawString("CANCEL", 235, 185, 2);
}

void DisplayManager::drawDeleteConfirmation(const String& path) {
  tft_.fillRect(0, 0, 320, 209, kBackground);
  tft_.fillRoundRect(18, 68, 284, 100, 12, kPanel);
  tft_.setTextColor(TFT_WHITE, kPanel);
  tft_.drawString("Delete this item?", 35, 83, 4);
  String name = path.substring(path.lastIndexOf('/') + 1);
  if (name.length() > 31) name = name.substring(0, 28) + "...";
  tft_.setTextColor(kMuted, kPanel);
  tft_.drawString(name, 35, 119, 2);
  tft_.fillRoundRect(35, 139, 112, 22, 5, 0xC800);
  tft_.fillRoundRect(171, 139, 112, 22, 5, kGreen);
  tft_.setTextColor(TFT_WHITE, 0xC800);
  tft_.drawString("CANCEL", 61, 143, 2);
  tft_.setTextColor(TFT_BLACK, kGreen);
  tft_.drawString("DELETE", 200, 143, 2);
}

void DisplayManager::setFileOffset(uint16_t offset) {
  fileOffset_ = offset - offset % 5;
}

void DisplayManager::setGalleryOffset(uint16_t offset) {
  galleryOffset_ = offset - offset % 5;
}

void DisplayManager::drawTransfer() {
  const TransferState transfer = getTransferState();
  tft_.setTextColor(TFT_WHITE, kBackground);
  tft_.drawString("Transfers", 14, 43, 4);
  tft_.fillRoundRect(12, 93, 296, 89, 10, kPanel);
  tft_.setTextColor(kMuted, kPanel);
  tft_.drawString(transfer.upload ? "UPLOAD" : "DOWNLOAD", 24, 105, 2);
  tft_.setTextColor(TFT_WHITE, kPanel);
  tft_.drawString(transfer.filename[0] ? transfer.filename : "No active transfer",
                  24, 130, 2);
  const uint64_t rawPercent =
      transfer.total
          ? static_cast<uint64_t>(transfer.transferred) * 100 / transfer.total
          : 0;
  const uint32_t percent = rawPercent > 100 ? 100 : rawPercent;
  tft_.drawRoundRect(23, 157, 268, 12, 5, kMuted);
  tft_.fillRoundRect(25, 159, 264 * percent / 100, 8, 4, kAccent);
  tft_.setTextColor(kMuted, kPanel);
  tft_.drawString(String(transfer.status) + "  " + String(percent) + "%",
                  23, 174, 2);
}

void DisplayManager::refreshStatusBar(WifiManager& wifi, FileManager& files) {
  drawStatusBar(wifi, files);
}

void DisplayManager::refreshTransfer() { drawTransfer(); }

void DisplayManager::drawWifiKeyboard(const String& field, const String& value,
                                     bool symbols, bool uppercase) {
  const char* letterRows[] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
  const char* symbolRows[] = {"1234567890", "@#$%&*-+()", "!?.,:_/;"};
  tft_.fillRect(0, 31, 320, 178, kBackground);
  tft_.setTextColor(TFT_WHITE, kBackground);
  tft_.drawString("WiFi " + field, 10, 37, 2);
  tft_.setTextColor(kAccent, kBackground);
  tft_.drawString("CANCEL", 268, 38, 1);
  tft_.fillRoundRect(8, 58, 304, 24, 5, kPanel);
  tft_.setTextColor(TFT_WHITE, kPanel);
  String shown = value;
  if (field == "Password") {
    shown = "";
    for (size_t i = 0; i < value.length(); ++i) shown += '*';
  }
  if (shown.length() > 38) shown = "..." + shown.substring(shown.length() - 35);
  tft_.drawString(shown + "_", 14, 63, 2);

  for (uint8_t row = 0; row < 3; ++row) {
    String keys = symbols ? symbolRows[row] : letterRows[row];
    if (!symbols && uppercase) keys.toUpperCase();
    const int keyWidth = 28;
    const int gap = 2;
    int startX = row == 1 ? 18 : (row == 2 && !symbols ? 41 : 5);
    for (uint8_t col = 0; col < keys.length(); ++col) {
      const int x = startX + col * (keyWidth + gap);
      const int y = 87 + row * 25;
      tft_.fillRoundRect(x, y, keyWidth, 22, 4, kPanel);
      tft_.setTextDatum(MC_DATUM);
      tft_.setTextColor(TFT_WHITE, kPanel);
      tft_.drawString(String(keys[col]), x + keyWidth / 2, y + 11, 2);
    }
  }
  tft_.setTextDatum(TL_DATUM);
  tft_.fillRoundRect(258, 137, 56, 22, 4, kOrange);
  tft_.setTextColor(TFT_BLACK, kOrange);
  tft_.drawString("DEL", 276, 141, 2);
  tft_.fillRoundRect(4, 163, 63, 22, 4, kPanel);
  tft_.fillRoundRect(70, 163, 55, 22, 4, kPanel);
  tft_.fillRoundRect(128, 163, 76, 22, 4, kPanel);
  tft_.fillRoundRect(207, 163, 109, 22, 4, kGreen);
  tft_.setTextColor(TFT_WHITE, kPanel);
  tft_.drawString(symbols ? "ABC" : "123", 19, 167, 1);
  tft_.drawString("SHIFT", 78, 167, 1);
  tft_.drawString("SPACE", 145, 167, 1);
  tft_.setTextColor(TFT_BLACK, kGreen);
  tft_.drawString(field == "Password" ? "SAVE" : "NEXT", 243, 167, 2);
}

void DisplayManager::drawWifiNotice(const String& message) {
  tft_.fillRect(8, 199, 304, 10, kBackground);
  tft_.setTextColor(kAccent, kBackground);
  String visible = message;
  if (visible.length() > 52) visible = visible.substring(0, 49) + "...";
  tft_.drawString(visible, 10, 200, 1);
}

void DisplayManager::drawSettings(WifiManager& wifi, bool wifiQr) {
  tft_.setTextColor(TFT_WHITE, kBackground);
  tft_.drawString("Settings", 14, 43, 4);
  tft_.fillRoundRect(12, 68, 106, 22, 5, kGreen);
  tft_.fillRoundRect(124, 68, 100, 22, 5, kPanel);
  tft_.setTextColor(TFT_BLACK, kGreen);
  tft_.drawString("SET WIFI", 34, 72, 2);
  tft_.setTextColor(kAccent, kPanel);
  tft_.drawString(wifiQr ? "DASH QR" : "WIFI QR", 145, 72, 2);
  tft_.fillRoundRect(12, 96, 296, 82, 10, kPanel);
  tft_.setTextColor(kMuted, kPanel);
  tft_.drawString(wifi.connected() ? "CONNECTED NETWORK" : "DEVICE ACCESS POINT",
                  23, 103, 1);
  tft_.setTextColor(TFT_WHITE, kPanel);
  String networkName = wifi.connected() ? wifi.ssid() : wifi.apSsid();
  if (networkName.length() > 23) networkName = networkName.substring(0, 20) + "...";
  tft_.drawString(networkName, 23, 116, 2);
  tft_.setTextColor(kMuted, kPanel);
  tft_.drawString(wifi.dashboardUrl(), 23, 138, 1);
  tft_.drawString("Brightness", 23, 160, 1);
  tft_.setTextColor(TFT_WHITE, kPanel);
  tft_.drawString(String(brightness_) + "%   -   +", 100, 157, 2);
  tft_.drawRoundRect(234, 96, 63, 63, 4, kMuted);
  tft_.fillRoundRect(12, 183, 106, 19, 5, kPanel);
  tft_.setTextColor(kAccent, kPanel);
  tft_.drawString("CALIBRATE", 24, 187, 1);
  tft_.fillRoundRect(124, 183, 100, 19, 5, kPanel);
  tft_.setTextColor(kAccent, kPanel);
  tft_.drawString("IMAGE TEST", 135, 187, 1);
  tft_.fillRoundRect(234, 183, 63, 19, 5, 0x6410);
  tft_.setTextDatum(MC_DATUM);
  tft_.setTextColor(TFT_WHITE, 0x6410);
  tft_.drawString("Restart", 265, 192, 1);
  tft_.setTextDatum(TL_DATUM);

  QRCode code;
  uint8_t data[qrcode_getBufferSize(3)];
  const String payload =
      wifiQr ? "WIFI:T:WPA;S:" + wifi.apSsid() + ";P:" + wifi.apPassword() + ";;"
             : wifi.dashboardUrl();
  qrcode_initText(&code, data, 3, ECC_LOW, payload.c_str());
  {
    const int scale = 2;
    const int originX = 241;
    const int originY = 100;
    for (uint8_t y = 0; y < code.size; ++y) {
      for (uint8_t x = 0; x < code.size; ++x) {
        tft_.fillRect(originX + x * scale, originY + y * scale, scale, scale,
                      qrcode_getModule(&code, x, y) ? TFT_BLACK : TFT_WHITE);
      }
    }
  }
}

void DisplayManager::drawPage(uint8_t page, WifiManager& wifi,
                              FileManager& files, bool wifiQr,
                              const String& galleryPath,
                              const String& filePath) {
  if (page == 2 && photoVisible_) return;
  tft_.fillScreen(kBackground);
  drawStatusBar(wifi, files);
  switch (page) {
    case 1:
      drawFiles(files, filePath);
      break;
    case 2:
      drawGallery(files, galleryPath);
      break;
    case 3:
      drawTransfer();
      break;
    case 4:
      drawSettings(wifi, wifiQr);
      break;
    default:
      drawHome(wifi, files);
      break;
  }
  drawNavigation(page);
  lastPage_ = page;
}
