#pragma once
// ============================================================================
//  Matrix_Display_RTOS - ALL tunable constants live here
// ============================================================================
#include <Arduino.h>
#include "I2Common.h"

#define FW_VERSION            "2.0-rtos"

// -------------------------------------------------------------------- matrix --
#define PIN_DIN               5
#define MATRIX_W              16        // test panel 16x8, final project 64x8
#define MATRIX_H              8
#define MATRIX_BRIGHTNESS     20
#define SCROLL_DELAY_MS       60        // smaller panel -> larger delay reads better
#define CONNECTING_DELAY_MS   100

// ---------------------------------------------------------------- ThingSpeak --
#define TS_READ_MS            30000UL   // normal reading period
#define TS_RETRY_MS           10000UL   // after a failed read
#define NO_DATA_RESTART_MS    (30UL * 60UL * 1000UL)   // WiFi up but no good read for 30 min -> restart

// ---------------------------------------------------------------- RTOS layout --
// The scroll must stay smooth while the (blocking) HTTP request runs, so the
// display has the higher priority and, on the ESP32, its own core.
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

// ------------------------------------------------------------ factory values --
#define DEFAULT_CHANNEL_ID    3095045UL
#define DEFAULT_READ_KEY      "JM9QEV7SW8RKWRQZ"
