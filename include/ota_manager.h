#pragma once

#include <Arduino.h>

class OtaManager {
 public:
  static bool begin(size_t imageSize, String& error);
  static bool write(const uint8_t* data, size_t length, String& error);
  static bool finish(String& error);
  static void abort();
};
