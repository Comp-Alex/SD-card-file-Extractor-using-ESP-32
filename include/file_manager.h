#pragma once

#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <memory>
#include <vfs_api.h>

class FileManager {
 public:
  FileManager();
  bool begin();
  bool beginTouchRead();
  void endTouchRead();
  bool retryMount();
  bool isMounted(bool external = false) const;
  bool beginFileRead(const String& path, FS*& volume, String& physicalPath,
                     String& error) const;
  void endFileRead() const;
  uint64_t totalBytes(bool external = false);
  uint64_t usedBytes(bool external = false);
  FS* fileSystemForPath(const String& path, String& physicalPath,
                        String& error) const;
  String listJson(const String& requestedPath, bool& ok, String& error);
  bool removePath(const String& path, String& error);
  bool renamePath(const String& from, const String& to, String& error);
  bool copyPath(const String& from, const String& to, bool move,
                String& error);
  bool makeDirectory(const String& path, String& error);
  bool beginUpload(const String& path, String& error);
  bool writeUpload(const uint8_t* data, size_t length, String& error);
  bool finishUpload(String& error);
  void abortUpload();
  static bool normalizePath(const String& input, String& output);
  static String contentType(const String& path);

 private:
  bool takeStorageLock(TickType_t timeout) const;
  void giveStorageLock() const;
  bool mountLocked(fs::SDFS& volume, int chipSelect, bool& mounted,
                   const char* label);
  bool ensureParentsLocked(FS& volume, const String& path);
  String normalizeOrRoot(const String& path, bool& valid) const;
  bool resolvePathLocked(const String& requestedPath, fs::SDFS*& volume,
                         String& physicalPath, String& logicalPrefix,
                         String& error) const;

  fs::FSImplPtr externalImpl_;
  mutable fs::SDFS externalSd_;
  mutable SemaphoreHandle_t mutex_ = nullptr;
  bool mounted_ = false;
  bool externalMounted_ = false;
  uint32_t lastMountAttempt_ = 0;
  uint32_t lastExternalMountAttempt_ = 0;
  File uploadFile_;
  fs::SDFS* uploadVolume_ = nullptr;
  bool uploadActive_ = false;
};
