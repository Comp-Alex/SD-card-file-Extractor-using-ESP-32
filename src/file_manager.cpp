#include "file_manager.h"

#include <ArduinoJson.h>
#include <new>
#include "cyd_config.h"
#include "transfer_state.h"

namespace {
String parentPath(const String& path) {
  const int slash = path.lastIndexOf('/');
  return slash <= 0 ? "/" : path.substring(0, slash);
}

String baseName(const String& path) {
  const int slash = path.lastIndexOf('/');
  return slash < 0 ? path : path.substring(slash + 1);
}
}  // namespace

FileManager::FileManager()
    : externalImpl_(fs::FSImplPtr(new (std::nothrow) VFSImpl())),
      externalSd_(externalImpl_) {}

bool FileManager::begin() {
  if (mutex_ == nullptr) mutex_ = xSemaphoreCreateMutex();
  if (mutex_ == nullptr) {
    Serial.println("[SD] Failed to create filesystem mutex");
    return false;
  }
  if (!takeStorageLock(pdMS_TO_TICKS(1000))) return false;
  if (!externalImpl_) {
    Serial.println("[SD] Failed to create external volume filesystem");
  }
  const bool internalOk = mountLocked(SD, CydPins::kSdCs, mounted_, "internal");
  lastMountAttempt_ = millis();
  const bool externalOk = externalImpl_ && mountLocked(
      externalSd_, CydPins::kExternalSdCs, externalMounted_, "external");
  lastExternalMountAttempt_ = millis();
  giveStorageLock();
  return internalOk || externalOk;
}

bool FileManager::takeStorageLock(TickType_t timeout) const {
  if (mutex_ == nullptr || xSemaphoreTake(mutex_, timeout) != pdTRUE)
    return false;
  const TransferState transfer = getTransferState();
  if (transfer.active && !transfer.upload) {
    xSemaphoreGive(mutex_);
    return false;
  }
  SPI.end();
  SPI.begin(CydPins::kSdSclk, CydPins::kSdMiso, CydPins::kSdMosi,
            CydPins::kSdCs);
  return true;
}

void FileManager::giveStorageLock() const {
  xSemaphoreGive(mutex_);
}

bool FileManager::beginTouchRead() {
  if (mutex_ == nullptr || xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE)
    return false;
  SPI.end();
  SPI.begin(CydPins::kTouchSclk, CydPins::kTouchMiso, CydPins::kTouchMosi,
            CydPins::kTouchCs);
  return true;
}

void FileManager::endTouchRead() {
  SPI.end();
  SPI.begin(CydPins::kSdSclk, CydPins::kSdMiso, CydPins::kSdMosi,
            CydPins::kSdCs);
  xSemaphoreGive(mutex_);
}

bool FileManager::mountLocked(fs::SDFS& volume, int chipSelect, bool& mounted,
                              const char* label) {
  if (mounted) volume.end();
  mounted = volume.begin(chipSelect, SPI, 10000000,
                         strcmp(label, "external") == 0 ? "/ext" : "/sd", 5);
  if (mounted) {
    Serial.printf("[SD] %s card mounted, capacity: %llu bytes\n", label,
                  static_cast<unsigned long long>(volume.cardSize()));
  } else {
    Serial.printf("[SD] %s card not mounted\n", label);
  }
  return mounted;
}

bool FileManager::retryMount() {
  if (!takeStorageLock(pdMS_TO_TICKS(1000))) return false;
  const uint32_t now = millis();
  if (mounted_ && SD.cardType() == CARD_NONE) {
    SD.end();
    mounted_ = false;
    Serial.println("[SD] Internal card removed");
  }
  if (externalMounted_ && externalSd_.cardType() == CARD_NONE) {
    externalSd_.end();
    externalMounted_ = false;
    Serial.println("[SD] External card removed");
  }
  if (!mounted_ && (lastMountAttempt_ == 0 ||
                    now - lastMountAttempt_ >= AppConfig::kSdRetryMs)) {
    lastMountAttempt_ = now;
    mountLocked(SD, CydPins::kSdCs, mounted_, "internal");
  }
  if (externalImpl_ && !externalMounted_ &&
      (lastExternalMountAttempt_ == 0 ||
       now - lastExternalMountAttempt_ >= AppConfig::kSdRetryMs)) {
    lastExternalMountAttempt_ = now;
    mountLocked(externalSd_, CydPins::kExternalSdCs, externalMounted_,
                "external");
  }
  const bool result = mounted_;
  giveStorageLock();
  return result;
}

bool FileManager::isMounted(bool external) const {
  if (!takeStorageLock(pdMS_TO_TICKS(250))) return false;
  const bool result = external ? externalMounted_ &&
                                     externalSd_.cardType() != CARD_NONE
                               : mounted_ && SD.cardType() != CARD_NONE;
  giveStorageLock();
  return result;
}

bool FileManager::beginFileRead(const String& path, FS*& volume,
                                String& physicalPath, String& error) const {
  if (!takeStorageLock(pdMS_TO_TICKS(1000))) {
    error = "Storage busy";
    return false;
  }
  fs::SDFS* selected = nullptr;
  String logicalPrefix;
  if (!resolvePathLocked(path, selected, physicalPath, logicalPrefix, error)) {
    giveStorageLock();
    return false;
  }
  volume = selected;
  return true;
}

void FileManager::endFileRead() const { giveStorageLock(); }

uint64_t FileManager::totalBytes(bool external) {
  if (!takeStorageLock(pdMS_TO_TICKS(500))) return 0;
  const uint64_t result =
      external ? (externalMounted_ ? externalSd_.totalBytes() : 0)
               : (mounted_ ? SD.totalBytes() : 0);
  giveStorageLock();
  return result;
}

uint64_t FileManager::usedBytes(bool external) {
  if (!takeStorageLock(pdMS_TO_TICKS(500))) return 0;
  const uint64_t result =
      external ? (externalMounted_ ? externalSd_.usedBytes() : 0)
               : (mounted_ ? SD.usedBytes() : 0);
  giveStorageLock();
  return result;
}

bool FileManager::resolvePathLocked(const String& requestedPath,
                                    fs::SDFS*& volume, String& physicalPath,
                                    String& logicalPrefix,
                                    String& error) const {
  String normalized;
  if (!normalizePath(requestedPath, normalized)) {
    error = "Invalid path";
    return false;
  }
  const bool external =
      normalized == "/external" || normalized.startsWith("/external/");
  const bool explicitInternal =
      normalized == "/internal" || normalized.startsWith("/internal/");
  if (external && !externalMounted_) {
    error = "External storage is not connected";
    return false;
  }
  if (!external && !mounted_) {
    error = "Internal SD card is unavailable";
    return false;
  }
  volume = external ? &externalSd_ : &SD;
  logicalPrefix = external ? "/external" : (explicitInternal ? "/internal" : "");
  if (external) {
    physicalPath = normalized.substring(String("/external").length());
  } else if (explicitInternal) {
    physicalPath = normalized.substring(String("/internal").length());
  } else {
    physicalPath = normalized;
  }
  if (physicalPath.isEmpty()) physicalPath = "/";
  return true;
}

FS* FileManager::fileSystemForPath(const String& path, String& physicalPath,
                                   String& error) const {
  if (!takeStorageLock(pdMS_TO_TICKS(500))) {
    error = "Storage busy";
    return nullptr;
  }
  fs::SDFS* volume = nullptr;
  String logicalPrefix;
  const bool valid =
      resolvePathLocked(path, volume, physicalPath, logicalPrefix, error);
  giveStorageLock();
  return valid ? volume : nullptr;
}

bool FileManager::normalizePath(const String& input, String& output) {
  if (input.length() > AppConfig::kMaxPathLength) return false;
  String candidate = input;
  candidate.replace('\\', '/');
  if (candidate.indexOf(':') >= 0) return false;

  output = "/";
  int start = 0;
  while (start < candidate.length()) {
    while (start < candidate.length() && candidate[start] == '/') ++start;
    if (start >= candidate.length()) break;
    int end = candidate.indexOf('/', start);
    if (end < 0) end = candidate.length();
    const String part = candidate.substring(start, end);
    if (part == "." || part == ".." || part.length() == 0 ||
        part.length() > 64) {
      return false;
    }
    for (size_t i = 0; i < part.length(); ++i) {
      const uint8_t c = part[i];
      if (c < 32 || c == 127) return false;
    }
    if (output.length() > 1) output += '/';
    output += part;
    start = end + 1;
  }
  return output.length() <= AppConfig::kMaxPathLength;
}

String FileManager::normalizeOrRoot(const String& path, bool& valid) const {
  String result;
  valid = normalizePath(path, result);
  return valid ? result : String();
}

String FileManager::listJson(const String& requestedPath, bool& ok,
                             String& error) {
  ok = false;
  bool valid = false;
  const String path = normalizeOrRoot(requestedPath, valid);
  if (!valid) {
    error = "Invalid path";
    return String();
  }
  if (!takeStorageLock(pdMS_TO_TICKS(2000))) {
    error = "Storage busy";
    return String();
  }
  fs::SDFS* volume = nullptr;
  String physicalPath;
  String logicalPrefix;
  if (!resolvePathLocked(path, volume, physicalPath, logicalPrefix, error)) {
    giveStorageLock();
    return String();
  }
  File dir = volume->open(physicalPath);
  if (!dir || !dir.isDirectory()) {
    error = "Folder not found";
    if (dir) dir.close();
    giveStorageLock();
    return String();
  }

  DynamicJsonDocument doc(8192);
  doc["path"] = path;
  JsonArray entries = doc.createNestedArray("entries");
  uint16_t count = 0;
  File entry = dir.openNextFile();
  while (entry && count < 150) {
    const String fullName = entry.name();
    const String name = baseName(fullName);
    JsonObject item = entries.createNestedObject();
    item["name"] = name;
    item["path"] = path == "/" ? "/" + name : path + "/" + name;
    item["directory"] = entry.isDirectory();
    item["size"] = entry.isDirectory() ? 0 : entry.size();
    item["extension"] = entry.isDirectory() || name.lastIndexOf('.') < 0
                            ? ""
                            : name.substring(name.lastIndexOf('.') + 1);
    item["created"] = "Unavailable on FAT";
    item["modified"] = "Unavailable on FAT";
    entry.close();
    ++count;
    entry = dir.openNextFile();
  }
  if (entry) entry.close();
  dir.close();

  doc["count"] = count;
  doc["truncated"] = count == 150;
  uint16_t folders = 0;
  for (JsonObject item : entries) {
    if (item["directory"].as<bool>()) ++folders;
  }
  doc["folders"] = folders;
  String response;
  const size_t written = serializeJson(doc, response);
  giveStorageLock();
  if (written == 0 || doc.overflowed()) {
    error = "Unable to serialize folder listing";
    return String();
  }
  ok = true;
  return response;
}

bool FileManager::ensureParentsLocked(FS& volume, const String& path) {
  String parent = parentPath(path);
  if (parent == "/") return true;
  String current;
  int start = 1;
  while (start <= parent.length()) {
    const int slash = parent.indexOf('/', start);
    const String part = slash < 0 ? parent.substring(start)
                                  : parent.substring(start, slash);
    current += "/" + part;
    if (!volume.exists(current) && !volume.mkdir(current)) return false;
    if (slash < 0) break;
    start = slash + 1;
  }
  return true;
}

bool FileManager::removePath(const String& rawPath, String& error) {
  bool valid = false;
  const String path = normalizeOrRoot(rawPath, valid);
  if (!valid || path == "/") {
    error = "Invalid path";
    return false;
  }
  if (!takeStorageLock(pdMS_TO_TICKS(2000))) {
    error = "Storage busy";
    return false;
  }
  bool result = false;
  fs::SDFS* volume = nullptr;
  String physicalPath;
  String logicalPrefix;
  if (!resolvePathLocked(path, volume, physicalPath, logicalPrefix, error)) {
  } else {
    if (physicalPath == "/") {
      error = "Cannot delete a storage root";
      giveStorageLock();
      return false;
    }
    File target = volume->open(physicalPath);
    if (!target) error = "File or folder not found";
    else {
      const bool isDirectory = target.isDirectory();
      target.close();
      result = isDirectory ? volume->rmdir(physicalPath)
                           : volume->remove(physicalPath);
      if (!result) error = "Delete failed; folder may not be empty";
    }
  }
  giveStorageLock();
  return result;
}

bool FileManager::renamePath(const String& rawFrom, const String& rawTo,
                             String& error) {
  bool fromValid = false;
  bool toValid = false;
  const String from = normalizeOrRoot(rawFrom, fromValid);
  const String to = normalizeOrRoot(rawTo, toValid);
  if (!fromValid || !toValid || from == "/" || to == "/") {
    error = "Invalid source or destination";
    return false;
  }
  if (!takeStorageLock(pdMS_TO_TICKS(2000))) {
    error = "Storage busy";
    return false;
  }
  bool result = false;
  fs::SDFS* fromVolume = nullptr;
  fs::SDFS* toVolume = nullptr;
  String physicalFrom;
  String physicalTo;
  String fromPrefix;
  String toPrefix;
  const bool fromOk =
      resolvePathLocked(from, fromVolume, physicalFrom, fromPrefix, error);
  const bool toOk =
      resolvePathLocked(to, toVolume, physicalTo, toPrefix, error);
  if (!fromOk || !toOk) {
  } else if (fromVolume != toVolume) {
    error = "Use copy or move to rename across storage volumes";
  } else if (physicalFrom == "/" || physicalTo == "/") {
    error = "Cannot rename a storage root";
  } else if (!fromVolume->exists(physicalFrom)) error = "Source not found";
  else if (fromVolume->exists(physicalTo)) error = "Destination already exists";
  else {
    File source = fromVolume->open(physicalFrom);
    const bool directory = source && source.isDirectory();
    if (source) source.close();
    if (directory && physicalTo.startsWith(physicalFrom + "/")) {
      error = "A folder cannot be moved inside itself";
    } else if (!ensureParentsLocked(*fromVolume, physicalTo)) {
      error = "Unable to create destination folder";
    } else {
      result = fromVolume->rename(physicalFrom, physicalTo);
      if (!result) error = "Rename failed";
    }
  }
  giveStorageLock();
  return result;
}

bool FileManager::copyPath(const String& rawFrom, const String& rawTo,
                           bool move, String& error) {
  bool fromValid = false;
  bool toValid = false;
  const String from = normalizeOrRoot(rawFrom, fromValid);
  const String to = normalizeOrRoot(rawTo, toValid);
  if (!fromValid || !toValid || from == "/" || to == "/" || from == to) {
    error = "Invalid source or destination";
    return false;
  }
  if (!takeStorageLock(pdMS_TO_TICKS(2000))) {
    error = "Storage busy";
    return false;
  }
  fs::SDFS* sourceVolume = nullptr;
  fs::SDFS* destinationVolume = nullptr;
  String sourcePath;
  String destinationPath;
  String sourcePrefix;
  String destinationPrefix;
  bool result = false;
  if (!resolvePathLocked(from, sourceVolume, sourcePath, sourcePrefix, error) ||
      !resolvePathLocked(to, destinationVolume, destinationPath,
                         destinationPrefix, error)) {
  } else if (sourcePath == "/" || destinationPath == "/") {
    error = "Cannot copy or move a storage root";
  } else if (!sourceVolume->exists(sourcePath)) {
    error = "Source not found";
  } else if (destinationVolume->exists(destinationPath)) {
    error = "Destination already exists";
  } else if (sourceVolume == destinationVolume && move) {
    File source = sourceVolume->open(sourcePath);
    const bool isDirectory = source && source.isDirectory();
    if (source) source.close();
    if (isDirectory) {
      error = "Only files can be copied or moved between locations";
    } else if (!ensureParentsLocked(*destinationVolume, destinationPath))
      error = "Unable to create destination folder";
    else {
      result = sourceVolume->rename(sourcePath, destinationPath);
      if (!result) error = "Move failed";
    }
  } else {
    File input = sourceVolume->open(sourcePath, FILE_READ);
    if (!input || input.isDirectory()) {
      if (input) input.close();
      error = "Source must be a file";
    } else if (!ensureParentsLocked(*destinationVolume, destinationPath)) {
      input.close();
      error = "Unable to create destination folder";
    } else {
      File output = destinationVolume->open(destinationPath, FILE_WRITE);
      if (!output) {
        input.close();
        error = "Unable to create destination file";
      } else {
        uint8_t buffer[1024];
        const size_t total = input.size();
        size_t remaining = total;
        const String transferName = baseName(destinationPath);
        setTransferState(transferName.c_str(), "Copying", 0, total, true, true);
        result = true;
        while (remaining > 0) {
          const size_t chunk = min(remaining, sizeof(buffer));
          const size_t read = input.read(buffer, chunk);
          if (read == 0 || output.write(buffer, read) != read) {
            result = false;
            error = "Copy failed while reading or writing storage";
            break;
          }
          remaining -= read;
          setTransferState(transferName.c_str(), "Copying", total - remaining,
                           total, true, true);
        }
        input.close();
        output.close();
        if (result && move && !sourceVolume->remove(sourcePath)) {
          result = false;
          error = "Copied file, but could not remove the source";
        }
        if (!result) destinationVolume->remove(destinationPath);
        setTransferState(transferName.c_str(), result ? "Complete" : "Failed",
                         total - remaining, total, false, true);
      }
    }
  }
  giveStorageLock();
  return result;
}

bool FileManager::makeDirectory(const String& rawPath, String& error) {
  bool valid = false;
  const String path = normalizeOrRoot(rawPath, valid);
  if (!valid || path == "/") {
    error = "Invalid folder path";
    return false;
  }
  if (!takeStorageLock(pdMS_TO_TICKS(2000))) {
    error = "Storage busy";
    return false;
  }
  bool result = false;
  fs::SDFS* volume = nullptr;
  String physicalPath;
  String logicalPrefix;
  if (!resolvePathLocked(path, volume, physicalPath, logicalPrefix, error)) {
  } else if (volume->exists(physicalPath)) error = "Path already exists";
  else if (!ensureParentsLocked(*volume, physicalPath)) error = "Unable to create folder";
  else result = volume->mkdir(physicalPath);
  giveStorageLock();
  return result;
}

bool FileManager::beginUpload(const String& rawPath, String& error) {
  bool valid = false;
  const String path = normalizeOrRoot(rawPath, valid);
  if (!valid || path == "/") {
    error = "Invalid upload path";
    return false;
  }
  if (!takeStorageLock(pdMS_TO_TICKS(2000))) {
    error = "Storage busy";
    return false;
  }
  bool result = false;
  fs::SDFS* volume = nullptr;
  String physicalPath;
  String logicalPrefix;
  if (uploadActive_) error = "Another upload is active";
  else if (!resolvePathLocked(path, volume, physicalPath, logicalPrefix, error)) {
  } else if (physicalPath == "/") {
    error = "Upload destination must be a file path";
  } else if (!ensureParentsLocked(*volume, physicalPath)) error = "Unable to create folder";
  else {
    uploadFile_ = volume->open(physicalPath, FILE_WRITE);
    uploadVolume_ = volume;
    uploadActive_ = static_cast<bool>(uploadFile_);
    result = uploadActive_;
    if (!result) error = "Unable to open destination file";
  }
  giveStorageLock();
  return result;
}

bool FileManager::writeUpload(const uint8_t* data, size_t length, String& error) {
  if (!takeStorageLock(pdMS_TO_TICKS(5000))) {
    error = "Storage busy";
    return false;
  }
  const bool result = uploadActive_ && uploadFile_ &&
                      uploadFile_.write(data, length) == length;
  if (!result) error = "SD write failed";
  giveStorageLock();
  return result;
}

bool FileManager::finishUpload(String& error) {
  if (!takeStorageLock(pdMS_TO_TICKS(2000))) {
    error = "Storage busy";
    return false;
  }
  const bool result = uploadActive_ && uploadFile_;
  if (uploadFile_) uploadFile_.close();
  uploadVolume_ = nullptr;
  uploadActive_ = false;
  if (!result) error = "No upload is active";
  giveStorageLock();
  return result;
}

void FileManager::abortUpload() {
  if (!takeStorageLock(pdMS_TO_TICKS(2000)))
    return;
  if (uploadFile_) uploadFile_.close();
  uploadVolume_ = nullptr;
  uploadActive_ = false;
  giveStorageLock();
}

String FileManager::contentType(const String& path) {
  String name = path;
  name.toLowerCase();
  if (name.endsWith(".html")) return "text/html";
  if (name.endsWith(".css")) return "text/css";
  if (name.endsWith(".js")) return "application/javascript";
  if (name.endsWith(".json")) return "application/json";
  if (name.endsWith(".txt") || name.endsWith(".log")) return "text/plain";
  if (name.endsWith(".jpg") || name.endsWith(".jpeg")) return "image/jpeg";
  if (name.endsWith(".png")) return "image/png";
  if (name.endsWith(".bmp")) return "image/bmp";
  if (name.endsWith(".pdf")) return "application/pdf";
  return "application/octet-stream";
}
