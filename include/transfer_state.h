#pragma once

#include <Arduino.h>

struct TransferState {
  char filename[64] = {};
  char status[24] = "Idle";
  uint32_t transferred = 0;
  uint32_t total = 0;
  uint32_t startedAt = 0;
  uint32_t lastUpdateAt = 0;
  bool active = false;
  bool upload = true;
};

void setTransferState(const char* filename, const char* status,
                      uint32_t transferred, uint32_t total, bool active,
                      bool upload);
TransferState getTransferState();
