#include "transfer_state.h"

namespace {
TransferState state;
portMUX_TYPE stateMux = portMUX_INITIALIZER_UNLOCKED;
}  // namespace

void setTransferState(const char* filename, const char* status,
                      uint32_t transferred, uint32_t total, bool active,
                      bool upload) {
  portENTER_CRITICAL(&stateMux);
  strlcpy(state.filename, filename ? filename : "", sizeof(state.filename));
  strlcpy(state.status, status ? status : "", sizeof(state.status));
  state.transferred = transferred;
  state.total = total;
  state.active = active;
  state.upload = upload;
  if (active && state.startedAt == 0) state.startedAt = millis();
  if (active) state.lastUpdateAt = millis();
  if (!active) {
    state.startedAt = 0;
    state.lastUpdateAt = 0;
  }
  portEXIT_CRITICAL(&stateMux);
}

TransferState getTransferState() {
  TransferState snapshot;
  portENTER_CRITICAL(&stateMux);
  snapshot = state;
  portEXIT_CRITICAL(&stateMux);
  return snapshot;
}
