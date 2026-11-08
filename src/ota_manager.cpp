#include "ota_manager.h"

#include <Update.h>

bool OtaManager::begin(size_t imageSize, String& error) {
  if (!Update.begin(imageSize == 0 ? UPDATE_SIZE_UNKNOWN : imageSize)) {
    error = Update.errorString();
    return false;
  }
  return true;
}

bool OtaManager::write(const uint8_t* data, size_t length, String& error) {
  if (Update.write(const_cast<uint8_t*>(data), length) != length) {
    error = Update.errorString();
    Update.abort();
    return false;
  }
  return true;
}

bool OtaManager::finish(String& error) {
  if (!Update.end(true)) {
    error = Update.errorString();
    return false;
  }
  return true;
}

void OtaManager::abort() { Update.abort(); }
