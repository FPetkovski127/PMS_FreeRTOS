// PMS5003T + sound level sensor unit (FreeRTOS)
//
// Tasks (priority in brackets, set in app_config.h):
//   slm_reader (4), pms_fsm (3), thingspeak (2), slm_calc (2), wifiTask (2, library),
//   display (1), led (1), console (1, library)
//
// Files: app_config.h (constants), app_state.* (shared data), task_pms.cpp (state machine),
// task_thingspeak.cpp (upload, calibration), sound_level.* (sound level meter),
// sensor_console.cpp (commands), pms5003t.* (frame parser), task_display.cpp, task_misc.cpp.
// The library I2Common must be in the Arduino libraries folder.
#include "app_config.h"
#include "app_state.h"
#include "sound_level.h"

static const I2Defaults DEFAULTS = {
  I2_ROLE_SENSOR, FW_VERSION, "PMS-Sensor",
  I2_DEFAULT_WIFI_SSID, I2_DEFAULT_WIFI_PASS,
  DEFAULT_CHANNEL_ID, DEFAULT_WRITE_KEY, "",
  I2_DEFAULT_ACCESS_CODE,
  I2_SERVICE_WIFI_SSID, I2_SERVICE_WIFI_PASS, I2_OTA_PASSWORD,
  I2_AP_SSID, I2_AP_PASSWORD,
  PMS_SLEEP_DEFAULT_S,
  DEFAULT_CAL_PM25, DEFAULT_CAL_PM10, DEFAULT_CAL_TEMP, DEFAULT_CAL_HUMID
};

static void onOtaStart() { slmSetActive(false); }       // stop the microphone while flashing

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.printf("\n=== PMS sensor unit, firmware %s ===\n", FW_VERSION);

  pinMode(PIN_PMS_SET, OUTPUT);
  digitalWrite(PIN_PMS_SET, LOW);
  pinMode(PIN_LED, OUTPUT);

  i2WdtInit(WDT_TIMEOUT_S);             // task watchdog
  i2cfgBegin(DEFAULTS);                 // load settings from flash
  appStateInit();
  perfBegin();                          // CPU idle baseline, before the other tasks start

  i2wifiSetOtaStartHook(onOtaStart);
  i2wifiBegin();

  i2CreateTask(pmsTask,     "pms_fsm",    5000, NULL, PRIO_PMS,     &g_pmsTask, CORE_NET);
  i2CreateTask(tsTask,      "thingspeak", 7168, NULL, PRIO_TS,      NULL,       CORE_NET);
  i2CreateTask(ledTask,     "led",        2048, NULL, PRIO_LED,     NULL,       CORE_NET);
#if ENABLE_OLED
  i2CreateTask(displayTask, "display",    5000, NULL, PRIO_DISPLAY, NULL,       CORE_NET);
#endif
  slmBegin();                           // does nothing if ENABLE_SLM is 0
  sensorConsoleInit();

  Serial.println("Setup done");
}

void loop() {
  vTaskDelay(portMAX_DELAY);            // all work is done in tasks
}
