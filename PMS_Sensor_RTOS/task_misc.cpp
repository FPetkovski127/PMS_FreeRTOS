// ============================================================================
//  Small housekeeping tasks: status LED and external hardware watchdog (555)
// ============================================================================
#include "app_state.h"

// LED toggles every LED_FAST_MS (fast mode) or LED_NORMAL_MS (normal mode).
void ledTask(void*) {
  for (;;) {
    digitalWrite(PIN_LED, !digitalRead(PIN_LED));
    vTaskDelay(pdMS_TO_TICKS(i2cfgFastMode() ? LED_FAST_MS : LED_NORMAL_MS));
  }
}

// Kicks the 555 only while the FSM is alive (heartbeat advanced). If the FSM
// hangs the pulses stop and the 555 resets the ESP32.
void hwWdtTask(void*) {
#if PIN_HW_WDT_KICK >= 0
  uint32_t lastBeat = g_pmsHeartbeat;
  uint32_t lastKick = 0;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    uint32_t beat = g_pmsHeartbeat;
    if (beat != lastBeat && millis() - lastKick >= HW_WDT_PERIOD_MS) {
      hwWdtKick();
      lastKick = millis();
    }
    lastBeat = beat;
  }
#else
  vTaskDelete(NULL);
#endif
}
