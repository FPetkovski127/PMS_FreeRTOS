#pragma once
// ============================================================================
//  I2Perf - real-time measurement tools (no special sdkconfig needed)
//
//   1. PerfProbe      : microsecond timing of a code section (count/last/min/
//                       avg/max + deadline-overrun counter). WCET = max.
//   2. PerfCounter    : event counters (queue drops, retries, ...).
//   3. CPU load       : per-core load (and peak) measured with FreeRTOS idle
//                       hooks, calibrated against an idle system at boot.
//   4. Task table     : priority, state and STACK HIGH-WATER MARK (free bytes)
//                       of every task created with i2CreateTask().
//   5. Heap watermark : free / minimum-ever-free / largest block.
//   6. Load generator : synthetic CPU load to find the breaking point.
// ============================================================================
#include <Arduino.h>
#include "I2Task.h"
#include "esp_timer.h"

struct PerfProbe {
  const char* name;
  uint32_t budgetUs;            // deadline / period (0 = none). us > budget -> overrun
  uint32_t count, lastUs, minUs, maxUs, overruns;
  uint64_t totalUs;
};
#define PERF_PROBE_INIT(nm, budget) { nm, (uint32_t)(budget), 0, 0, 0xFFFFFFFFu, 0, 0, 0ULL }

struct PerfCounter {
  const char* name;
  volatile uint32_t value;
};
#define PERF_COUNTER_INIT(nm) { nm, 0 }

inline uint32_t perfNowUs() { return (uint32_t)esp_timer_get_time(); }

void perfAddProbe(PerfProbe* p);
void perfAddCounter(PerfCounter* c);
void perfRecord(PerfProbe* p, uint32_t us);
inline void perfCount(PerfCounter& c) { c.value = c.value + 1; }

// RAII helper:  { PerfScope s(pr_fft); ...code...; }
class PerfScope {
 public:
  explicit PerfScope(PerfProbe& p) : _p(p), _t0(perfNowUs()) {}
  ~PerfScope() { perfRecord(&_p, perfNowUs() - _t0); }
 private:
  PerfProbe& _p;
  uint32_t _t0;
};

void perfBegin();                       // call once in setup(): idle baseline (blocks ~300 ms)
void perfCpuEnable(bool on);            // start/stop CPU-load sampling (1 s window)
bool perfCpuEnabled();
void perfReport(Print& out);
void perfReset();
// Burns pct % of one core in 100 ms periods for 'seconds'. pct <= 95.
bool perfLoadStart(uint8_t pct, UBaseType_t prio, BaseType_t core, uint32_t seconds);
