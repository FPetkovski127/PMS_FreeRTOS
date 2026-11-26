#pragma once
// constants of the matrix display unit
#include <Arduino.h>
#include "I2Common.h"

#define FW_VERSION            "2.0-rtos"

// matrix
#define PIN_DIN               5
#define MATRIX_W              16        // test panel 16x8, final display 64x8
#define MATRIX_H              8
#define MATRIX_BRIGHTNESS     20
#define SCROLL_DELAY_MS       60
#define CONNECTING_DELAY_MS   100

// ThingSpeak
#define TS_READ_MS            30000UL   // reading period
#define TS_RETRY_MS           10000UL   // after a failed read
#define NO_DATA_RESTART_MS    (30UL * 60UL * 1000UL)   // restart if WiFi is up but nothing was read for 30 min

// task watchdog timeout, the chip resets when the display task hangs
#define WDT_TIMEOUT_S         10

// tasks (the display has a higher priority and its own core so the scrolling
// is not delayed by the HTTP request)
#define TEST_SINGLE_CORE      0
#define PRIO_DISPLAY          3
#define PRIO_TS               2
#if TEST_SINGLE_CORE
  #define CORE_DISPLAY        0
  #define CORE_NET            0
#else
  #define CORE_DISPLAY        I2_CORE_APP
  #define CORE_NET            I2_CORE_PRO
#endif

// factory values
#define DEFAULT_CHANNEL_ID    3095045UL
#define DEFAULT_READ_KEY      "JM9QEV7SW8RKWRQZ"
