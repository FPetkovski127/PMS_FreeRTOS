#pragma once
#include <Arduino.h>
#include "sdkconfig.h"

// Number of CPU cores of the target (ESP32 = 2, ESP32-S2 / C3 = 1).
#if (defined(CONFIG_FREERTOS_UNICORE) && CONFIG_FREERTOS_UNICORE) || defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32C3)
  #define I2_NUM_CORES 1
#else
  #define I2_NUM_CORES 2
#endif

#define I2_CORE_PRO 0                 // WiFi runs here
#define I2_CORE_APP 1                 // Arduino loop() runs here
#define I2_CORE_ANY tskNO_AFFINITY

// single-core chips always use core 0
inline BaseType_t i2Core(BaseType_t wanted) {
  if (wanted == tskNO_AFFINITY) return wanted;
  return (I2_NUM_CORES > 1) ? wanted : 0;
}

// Creates a pinned task and registers it for the perf report. Stack size in bytes.
BaseType_t i2CreateTask(TaskFunction_t fn, const char* name, uint32_t stackBytes,
                        void* arg, UBaseType_t prio, TaskHandle_t* out, BaseType_t core);

// Task watchdog: a timeout resets the chip. Idle tasks are watched too.
// A task registers itself with i2WdtAddSelf() and calls i2WdtFeed() regularly.
void i2WdtInit(uint32_t timeoutS);
void i2WdtAddSelf();
void i2WdtFeed();
