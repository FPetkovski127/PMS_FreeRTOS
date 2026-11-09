// ============================================================================
//  PMS5003T + sound level sensor unit  -  FreeRTOS version
//
//  Tasks (priority / core is set in app_config.h):
//    slm_reader  (4)  I2S block + IIR filters            [ENABLE_SLM]
//    pms_fsm     (3)  WARMUP -> MEASURE -> SLEEP state machine, UART parsing
//    thingspeak  (2)  uploads, retries, remote calibration check
//    slm_calc    (2)  dB / Leq / FFT                      [ENABLE_SLM]
//    wifiTask    (2)  connection supervisor + OTA         (library)
//    display, led (1) OLED / status LED
//    console     (1)  serial commands                     (library)
//
//  Files:  app_config.h   all constants          app_state.*   shared data, probes
//          task_pms.cpp   FSM                    task_thingspeak.cpp  network + calibration
//          sound_level.*  SLM                    sensor_console.cpp   perf / slm / cal commands
//          pms5003t.*     frame parser           task_display.cpp, task_misc.cpp
//  Library: I2Common (copy to Documents/Arduino/libraries)
//  Required libraries: ThingSpeak, Adafruit SSD1306 + GFX, arduinoFFT (if SLM_ENABLE_FFT)
// ============================================================================
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

static void onOtaStart() { slmSetActive(false); }       // free the CPU / I2S while flashing

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.printf("\n=== PMS sensor unit, firmware %s ===\n", FW_VERSION);

  pinMode(PIN_PMS_SET, OUTPUT);
  digitalWrite(PIN_PMS_SET, LOW);
  pinMode(PIN_LED, OUTPUT);
#if PIN_HW_WDT_KICK >= 0
  pinMode(PIN_HW_WDT_KICK, OUTPUT);
  digitalWrite(PIN_HW_WDT_KICK, !HW_WDT_ACTIVE_LEVEL);
#endif

  i2cfgBegin(DEFAULTS);                 // settings from flash (or factory defaults)
  i2cfgSetPreWriteHook(hwWdtKick);      // kick the 555 right before every flash write
  appStateInit();
  perfBegin();                          // CPU idle baseline - before the other tasks start

  i2wifiSetOtaStartHook(onOtaStart);
  i2wifiBegin();

  i2CreateTask(pmsTask,     "pms_fsm",    5000, NULL, PRIO_PMS,     &g_pmsTask, CORE_NET);
  i2CreateTask(tsTask,      "thingspeak", 7168, NULL, PRIO_TS,      NULL,       CORE_NET);
  i2CreateTask(ledTask,     "led",        2048, NULL, PRIO_LED,     NULL,       CORE_NET);
#if ENABLE_OLED
  i2CreateTask(displayTask, "display",    5000, NULL, PRIO_DISPLAY, NULL,       CORE_NET);
#endif
#if PIN_HW_WDT_KICK >= 0
  i2CreateTask(hwWdtTask,   "hw_wdt",     2048, NULL, PRIO_PMS,     NULL,       CORE_NET);
#endif
  slmBegin();                           // no-op when ENABLE_SLM is 0
  sensorConsoleInit();

  Serial.println("Setup done");
}

void loop() {
  vTaskDelay(portMAX_DELAY);            // everything runs in tasks
}
