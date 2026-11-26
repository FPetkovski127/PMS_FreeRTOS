#pragma once
// constants of the sensor unit
#include <Arduino.h>
#include "I2Common.h"

#define FW_VERSION            "2.0-rtos"

// features
#define ENABLE_SLM            1        // sound level meter (INMP441)
#define SLM_ENABLE_FFT        1        // spectrum on request ('slm fft'), needs arduinoFFT
#define ENABLE_OLED           1        // OLED used on the bench, the matrix unit is the final display

// 1 = all tasks on core 0 (behaves like the single-core ESP32-S2)
#define TEST_SINGLE_CORE      0

// pins
#define PIN_PMS_SET           4        // PMS5003T SET (HIGH = fan on, LOW = sleep)
#define PIN_PMS_RX            16       // ESP32 RX <- PMS TX
#define PIN_PMS_TX            17       // ESP32 TX -> PMS RX
#define PIN_LED               2
#define OLED_ADDRESS          0x3C     // I2C, SDA 21 / SCL 22

#define PIN_I2S_SCK           32       // INMP441 SCK
#define PIN_I2S_WS            5        // INMP441 WS
#define PIN_I2S_SD            33       // INMP441 SD (L/R pin to GND)

// With the L/R pin on GND the left channel gave only zeros on this core version,
// the right channel works. Use I2S_CHANNEL_FMT_ONLY_LEFT if it is the other way round.
#define SLM_I2S_CHANNEL       I2S_CHANNEL_FMT_ONLY_RIGHT
#define SLM_USE_APLL          0

// task watchdog timeout, the chip resets when a watched task hangs
#define WDT_TIMEOUT_S         10

// state machine timing
#define PMS_WARMUP_MS         30000UL  // fan on, data discarded
#define PMS_MEASURE_MS        30000UL  // samples are collected and averaged (normal mode)
#define PMS_FAST_WINDOW_MS    60000UL  // measuring window in fast mode
#define PMS_SLEEP_DEFAULT_S   240      // sleep time in normal mode, changeable with 'set sleep'
#define PMS_LOOP_MS           50       // loop period of the state machine
#define LED_NORMAL_MS         500      // LED toggle period
#define LED_FAST_MS           250

// ThingSpeak
#define TS_MIN_GAP_MS         16000UL  // minimum time between two writes
#define TS_RETRIES            3
#define TS_RETRY_WAIT_MS      20000UL
#define CAL_FIRST_CHECK_MS    60000UL  // calibration channel: first check after boot
#define CAL_CHECK_PERIOD_MS   3600000UL// then every hour
#define CAL_RETRY_MS          600000UL // after a failed check

// task priorities (higher number = higher priority)
#define PRIO_PERF             5
#define PRIO_SLM_READER       4
#define PRIO_PMS              3
#define PRIO_TS               2
#define PRIO_SLM_CALC         2
#define PRIO_DISPLAY          1
#define PRIO_LED              1

#if TEST_SINGLE_CORE
  #define CORE_NET            0
  #define CORE_SLM            0
#else
  #define CORE_NET            I2_CORE_PRO   // same core as the WiFi stack
  #define CORE_SLM            I2_CORE_APP
#endif

// PMS5003T frame
#define PMS_USE_ATMOSPHERIC   1        // 1 = atmospheric PM values, 0 = CF=1 values
#define PMS_QUEUE_LEN         4        // averages waiting for the upload task

// factory values
#define DEFAULT_CHANNEL_ID    3095045UL
#define DEFAULT_WRITE_KEY     "WKQQSKL49OZQKBB6"
// default calibration {gain, offset}, used until other values are stored
#define DEFAULT_CAL_PM25      {1.0f, 0.0f}
#define DEFAULT_CAL_PM10      {1.0f, 0.0f}
#define DEFAULT_CAL_TEMP      {1.0f, 0.0f}
#define DEFAULT_CAL_HUMID     {1.0f, 0.0f}
