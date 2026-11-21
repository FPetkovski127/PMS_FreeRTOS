// ============================================================================
//  Small housekeeping task: status LED
// ============================================================================
#include "app_state.h"

// LED toggles every LED_FAST_MS (fast mode) or LED_NORMAL_MS (normal mode).
void ledTask(void*) {
  for (;;) {
    digitalWrite(PIN_LED, !digitalRead(PIN_LED));
    vTaskDelay(pdMS_TO_TICKS(i2cfgFastMode() ? LED_FAST_MS : LED_NORMAL_MS));
  }
}
