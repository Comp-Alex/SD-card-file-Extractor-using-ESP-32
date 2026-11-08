#pragma once

#include <Arduino.h>

namespace CydPins {
constexpr int kTftSclk = 14;
constexpr int kTftMiso = 12;
constexpr int kTftMosi = 13;
constexpr int kTftCs = 15;
constexpr int kTftDc = 2;
constexpr int kBacklight = 21;
constexpr int kTouchCs = 33;
constexpr int kTouchIrq = 36;
constexpr int kTouchSclk = 25;
constexpr int kTouchMiso = 39;
constexpr int kTouchMosi = 32;
constexpr int kSdCs = 5;
constexpr int kSdSclk = 18;
constexpr int kSdMiso = 19;
constexpr int kSdMosi = 23;
constexpr int kExternalSdCs = 22;
}  // namespace CydPins

namespace AppConfig {
constexpr char kDeviceName[] = "ESP32 File Hub";
constexpr char kApPrefix[] = "FileHub-";
constexpr char kDefaultApSsid[] = "ESP32-FileHub";
constexpr char kDefaultApPassword[] = "FileHub123";
constexpr char kDefaultDashboardUser[] = "admin";
constexpr char kDefaultDashboardPassword[] = "admin12345";
constexpr uint32_t kSessionTimeoutMs = 15UL * 60UL * 1000UL;
constexpr uint32_t kSdRetryMs = 5000;
constexpr size_t kMaxPathLength = 192;
}  // namespace AppConfig
