#pragma once
// ============================================================================
//  PMS_Sensor_RTOS - ALL tunable constants live here
// ============================================================================
#include <Arduino.h>
#include "I2Common.h"

#define FW_VERSION            "2.0-rtos"

// ---------------------------------------------------------------- features --
#define ENABLE_SLM            1        // sound level meter (INMP441). 0 = PMS only
#define SLM_ENABLE_FFT        1        // on-demand spectrum ('slm fft'), needs arduinoFFT
#define ENABLE_OLED           1        // temporary display (final product: matrix unit)

// Test switch for the real-time analysis: 1 = put EVERY task on core 0 so the
// ESP32 NodeMCU behaves like the single-core ESP32-S2 mini.
#define TEST_SINGLE_CORE      0

// -------------------------------------------------------------------- pins --
#define PIN_PMS_SET           4        // PMS5003T SET   (HIGH = fan on, LOW = sleep)
#define PIN_PMS_RX            16       // ESP32 RX  <- PMS TX
#define PIN_PMS_TX            17       // ESP32 TX  -> PMS RX
#define PIN_LED               2
#define OLED_ADDRESS          0x3C     // I2C: SDA 21 / SCL 22 (ESP32 default)

#define PIN_I2S_SCK           32       // INMP441 SCK
#define PIN_I2S_WS            5        // INMP441 WS
#define PIN_I2S_SD            33       // INMP441 SD   (L/R pin of the mic -> GND)

// External 555 hardware watchdog. -1 = not wired / not used.
// The unit pulses this pin while it is healthy; if the pulses stop the 555
// resets the ESP32 (EN pin).
#define PIN_HW_WDT_KICK       -1
#define HW_WDT_ACTIVE_LEVEL   HIGH
#define HW_WDT_PULSE_MS       50
#define HW_WDT_PERIOD_MS      10000UL

// ----------------------------------------------------------- FSM timings ----
#define PMS_WARMUP_MS         30000UL  // state 1: fan on, sensor warming up (data discarded)
#define PMS_MEASURE_MS        30000UL  // state 2: normal mode, collect samples -> average -> ThingSpeak
#define PMS_FAST_WINDOW_MS    60000UL  // fast mode: window between two ThingSpeak updates
#define PMS_SLEEP_DEFAULT_S   240      // state 3 (normal mode only); runtime value = 'set sleep'
#define PMS_LOOP_MS           50       // FSM / UART polling period
#define LED_NORMAL_MS         500      // LED toggle period, normal mode
#define LED_FAST_MS           250      // LED toggle period, fast mode

// -------------------------------------------------------------- ThingSpeak --
#define TS_MIN_GAP_MS         16000UL  // free account: >= 15 s between writes
#define TS_RETRIES            3
#define TS_RETRY_WAIT_MS      20000UL
#define CAL_FIRST_CHECK_MS    60000UL  // first calibration check after boot
#define CAL_CHECK_PERIOD_MS   3600000UL// then every hour
#define CAL_RETRY_MS          600000UL // after a failed check

// ----------------------------------------------- task layout (RTOS design) --
// Priority: higher number = more urgent. Rate-monotonic idea: the shortest /
// most time-critical period gets the highest priority.
#define PRIO_PERF             5        // perf sampler (sleeps 1 s, runs ~us)
#define PRIO_SLM_READER       4        // I2S block every 125 ms - must never be late
#define PRIO_PMS              3        // FSM + UART, 50 ms
#define PRIO_TS               2        // HTTP (blocking, seconds)
#define PRIO_SLM_CALC         2
#define PRIO_DISPLAY          1
#define PRIO_LED              1

#if TEST_SINGLE_CORE
  #define CORE_NET            0
  #define CORE_SLM            0
#else
  #define CORE_NET            I2_CORE_PRO   // WiFi lives here -> PMS/TS/OLED next to it
  #define CORE_SLM            I2_CORE_APP   // sound processing on its own core
#endif

// ---------------------------------------------------------- PMS5003T frame --
#define PMS_USE_ATMOSPHERIC   1        // 1 = "atmospheric environment" PM values, 0 = CF=1 (factory)
#define PMS_QUEUE_LEN         4        // averaged results waiting for the ThingSpeak task

// ---------------------------------------------------------- factory values --
#define DEFAULT_CHANNEL_ID    3095045UL
#define DEFAULT_WRITE_KEY     "WKQQSKL49OZQKBB6"
// Calibration used until a unit-specific set is stored (gain, offset).
// Move the values that were hard-coded in the old PMS5003T.h here.
#define DEFAULT_CAL_PM25      {1.0f, 0.0f}
#define DEFAULT_CAL_PM10      {1.0f, 0.0f}
#define DEFAULT_CAL_TEMP      {1.0f, 0.0f}
#define DEFAULT_CAL_HUMID     {1.0f, 0.0f}
