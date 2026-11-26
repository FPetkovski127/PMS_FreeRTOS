#pragma once
// Timing and load measurement helpers: probes (microsecond timing of a code section),
// counters, CPU load per core (idle hook), task stack/heap report and a load generator.
#include <Arduino.h>
#include "I2Task.h"
#include "esp_timer.h"

struct PerfProbe {
  const char* name;
  uint32_t budgetUs;            // deadline / period, 0 = none
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

// measures the scope it is declared in
class PerfScope {
 public:
  explicit PerfScope(PerfProbe& p) : _p(p), _t0(perfNowUs()) {}
  ~PerfScope() { perfRecord(&_p, perfNowUs() - _t0); }
 private:
  PerfProbe& _p;
  uint32_t _t0;
};

void perfBegin();                       // call once in setup(), takes about 300 ms (idle baseline)
void perfCpuEnable(bool on);            // CPU load sampling, 1 s window
bool perfCpuEnabled();
void perfReport(Print& out);
void perfReset();
// uses pct percent of one core in 100 ms periods for 'seconds' (pct <= 95)
bool perfLoadStart(uint8_t pct, UBaseType_t prio, BaseType_t core, uint32_t seconds);
