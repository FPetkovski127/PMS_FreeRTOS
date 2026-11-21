#pragma once
#include <Arduino.h>
#include "sdkconfig.h"

// Number of CPU cores of the target (ESP32 = 2, ESP32-S2 / C3 = 1).
#if (defined(CONFIG_FREERTOS_UNICORE) && CONFIG_FREERTOS_UNICORE) || defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32C3)
  #define I2_NUM_CORES 1
#else
  #define I2_NUM_CORES 2
#endif

#define I2_CORE_PRO 0                 // WiFi / lwIP live here on the ESP32
#define I2_CORE_APP 1                 // Arduino loop() lives here
#define I2_CORE_ANY tskNO_AFFINITY

// Maps a wanted core to a valid one (single core targets always use core 0).
inline BaseType_t i2Core(BaseType_t wanted) {
  if (wanted == tskNO_AFFINITY) return wanted;
  return (I2_NUM_CORES > 1) ? wanted : 0;
}

// Creates a task, pins it (portable for 1 and 2 core chips) and registers it
// in the perf monitor (stack high-water mark, state, priority).
// Stack size is in BYTES (ESP-IDF FreeRTOS).
BaseType_t i2CreateTask(TaskFunction_t fn, const char* name, uint32_t stackBytes,
                        void* arg, UBaseType_t prio, TaskHandle_t* out, BaseType_t core);

// Internal task watchdog of the ESP32 (no external parts needed).
//  i2WdtInit    : timeout in seconds; a timeout RESETS the chip (panic). The idle tasks of all
//                 cores are watched, so a task that never blocks (starves the idle task) is detected.
//  i2WdtAddSelf : the calling task registers itself ...
//  i2WdtFeed    : ... and must call this regularly (more often than the timeout).
void i2WdtInit(uint32_t timeoutS);
void i2WdtAddSelf();
void i2WdtFeed();
