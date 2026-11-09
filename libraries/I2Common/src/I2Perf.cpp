#include "I2Perf.h"
#include "esp_freertos_hooks.h"
#include <string.h>

#define PERF_MAX_PROBES   24
#define PERF_MAX_COUNTERS 12
#define PERF_MAX_TASKS    16

static PerfProbe*   s_probes[PERF_MAX_PROBES];     static uint8_t s_nProbes = 0;
static PerfCounter* s_counters[PERF_MAX_COUNTERS]; static uint8_t s_nCounters = 0;
struct TaskRec { TaskHandle_t h; const char* name; };
static TaskRec s_tasks[PERF_MAX_TASKS];            static uint8_t s_nTasks = 0;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

// ---------------------------------------------------------------------------
BaseType_t i2CreateTask(TaskFunction_t fn, const char* name, uint32_t stackBytes,
                        void* arg, UBaseType_t prio, TaskHandle_t* out, BaseType_t core) {
  TaskHandle_t h = nullptr;
  BaseType_t r = xTaskCreatePinnedToCore(fn, name, stackBytes, arg, prio, &h, i2Core(core));
  if (r == pdPASS) {
    if (s_nTasks < PERF_MAX_TASKS) { s_tasks[s_nTasks].h = h; s_tasks[s_nTasks].name = name; s_nTasks++; }
    if (out) *out = h;
  } else {
    Serial.printf("[perf] FAILED to create task %s (not enough heap?)\n", name);
  }
  return r;
}

void perfAddProbe(PerfProbe* p)     { if (s_nProbes < PERF_MAX_PROBES) s_probes[s_nProbes++] = p; }
void perfAddCounter(PerfCounter* c) { if (s_nCounters < PERF_MAX_COUNTERS) s_counters[s_nCounters++] = c; }

void perfRecord(PerfProbe* p, uint32_t us) {
  portENTER_CRITICAL(&s_mux);
  p->count++;
  p->lastUs = us;
  if (us < p->minUs) p->minUs = us;
  if (us > p->maxUs) p->maxUs = us;
  p->totalUs += us;
  if (p->budgetUs && us > p->budgetUs) p->overruns++;
  portEXIT_CRITICAL(&s_mux);
}

// ---------------------------------------------------------------------------
//  CPU load through idle hooks
//  The hook returns false (= "do not enter low-power wait") so the idle task
//  spins and counts. Load = 1 - idleRate / idleRateAtBoot. Only active while
//  'perf cpu on' - it costs power but nothing else.
// ---------------------------------------------------------------------------
static volatile uint32_t s_idleCnt[2] = {0, 0};
static bool idleHook0() { s_idleCnt[0] = s_idleCnt[0] + 1; return false; }
static bool idleHook1() { s_idleCnt[1] = s_idleCnt[1] + 1; return false; }

static double  s_base[2] = {0, 0};          // idle iterations / second at 0 % load
static bool    s_baseOk = false;
static bool    s_cpuOn = false, s_resync = false;
static float   s_load[2] = {0, 0}, s_peak[2] = {0, 0};
static double  s_sum[2] = {0, 0};
static uint32_t s_nSamples = 0;
static TaskHandle_t s_sampler = nullptr;

static void hooksOn() {
  esp_register_freertos_idle_hook_for_cpu(idleHook0, 0);
#if I2_NUM_CORES > 1
  esp_register_freertos_idle_hook_for_cpu(idleHook1, 1);
#endif
}
static void hooksOff() {
  esp_deregister_freertos_idle_hook_for_cpu(idleHook0, 0);
#if I2_NUM_CORES > 1
  esp_deregister_freertos_idle_hook_for_cpu(idleHook1, 1);
#endif
}

void perfBegin() {
  uint32_t c0[2] = {s_idleCnt[0], s_idleCnt[1]};
  int64_t t0 = esp_timer_get_time();
  hooksOn();
  vTaskDelay(pdMS_TO_TICKS(300));
  hooksOff();
  double dt = (double)(esp_timer_get_time() - t0);
  for (int c = 0; c < I2_NUM_CORES; c++) {
    uint32_t d = s_idleCnt[c] - c0[c];
    s_base[c] = (double)d * 1e6 / dt;
  }
  s_baseOk = (s_base[0] > 1000.0);
  Serial.printf("[perf] idle baseline: core0=%.0f/s core1=%.0f/s %s\n", s_base[0], s_base[1],
                s_baseOk ? "" : "(hook not working - CPU load unavailable)");
}

static void samplerTask(void*) {
  uint32_t last[2] = {0, 0};
  int64_t lastT = 0;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    if (!s_cpuOn) continue;
    int64_t now = esp_timer_get_time();
    uint32_t cur[2] = {s_idleCnt[0], s_idleCnt[1]};
    if (s_resync) { last[0] = cur[0]; last[1] = cur[1]; lastT = now; s_resync = false; continue; }
    double dt = (double)(now - lastT);
    for (int c = 0; c < I2_NUM_CORES; c++) {
      double rate = (double)(cur[c] - last[c]) * 1e6 / dt;
      double load = 100.0 * (1.0 - rate / s_base[c]);
      if (load < 0) load = 0;
      if (load > 100) load = 100;
      s_load[c] = (float)load;
      if (s_load[c] > s_peak[c]) s_peak[c] = s_load[c];
      s_sum[c] += load;
      last[c] = cur[c];
    }
    s_nSamples++;
    lastT = now;
  }
}

void perfCpuEnable(bool on) {
  if (on) {
    if (!s_baseOk) { Serial.println("[perf] no idle baseline - call perfBegin() first"); return; }
    if (!s_sampler) xTaskCreatePinnedToCore(samplerTask, "perfSampler", 3072, NULL, 5, &s_sampler, i2Core(I2_CORE_ANY));
    perfReset();
    hooksOn();
    s_resync = true;
    s_cpuOn = true;
  } else if (s_cpuOn) {
    s_cpuOn = false;
    hooksOff();
  }
}
bool perfCpuEnabled() { return s_cpuOn; }

// ---------------------------------------------------------------------------
void perfReset() {
  portENTER_CRITICAL(&s_mux);
  for (uint8_t i = 0; i < s_nProbes; i++) {
    PerfProbe* p = s_probes[i];
    p->count = p->lastUs = p->maxUs = p->overruns = 0;
    p->minUs = 0xFFFFFFFFu;
    p->totalUs = 0;
  }
  portEXIT_CRITICAL(&s_mux);
  for (uint8_t i = 0; i < s_nCounters; i++) s_counters[i]->value = 0;
  s_peak[0] = s_peak[1] = 0;
  s_sum[0] = s_sum[1] = 0;
  s_nSamples = 0;
}

static const char* stateName(eTaskState s) {
  switch (s) {
    case eRunning:   return "run";
    case eReady:     return "ready";
    case eBlocked:   return "block";
    case eSuspended: return "susp";
    case eDeleted:   return "del";
    default:         return "?";
  }
}

void perfReport(Print& out) {
  out.printf("\n===== PERF REPORT  uptime %lu s =====\n", (unsigned long)(millis() / 1000));

  if (s_cpuOn && s_nSamples) {
    for (int c = 0; c < I2_NUM_CORES; c++)
      out.printf("CPU core%d: now %.0f%%  avg %.0f%%  PEAK %.0f%%  (%lu x 1 s samples)\n", c,
                 s_load[c], s_sum[c] / s_nSamples, s_peak[c], (unsigned long)s_nSamples);
  } else {
    out.println("CPU load: off  (enable with 'perf cpu on', wait a few seconds, then 'perf')");
  }

  out.printf("Heap: free %u  min-ever %u  largest block %u\n",
             (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(), (unsigned)ESP.getMaxAllocHeap());

  out.println("--- tasks (stack = minimum FREE bytes ever; <300 is dangerous) ---");
  out.println("name            prio state  stack_free");
  for (uint8_t i = 0; i < s_nTasks; i++) {
    out.printf("%-15s %4u %-6s %u\n", s_tasks[i].name, (unsigned)uxTaskPriorityGet(s_tasks[i].h),
               stateName(eTaskGetState(s_tasks[i].h)), (unsigned)uxTaskGetStackHighWaterMark(s_tasks[i].h));
  }

  out.println("--- timing probes (us) ---");
  out.println("name              count     last      min      avg      MAX   budget  overruns");
  for (uint8_t i = 0; i < s_nProbes; i++) {
    PerfProbe c;
    portENTER_CRITICAL(&s_mux);
    c = *s_probes[i];
    portEXIT_CRITICAL(&s_mux);
    uint32_t avg = c.count ? (uint32_t)(c.totalUs / c.count) : 0;
    out.printf("%-16s %6lu %8lu %8lu %8lu %8lu %8lu %8lu\n", c.name, (unsigned long)c.count,
               (unsigned long)c.lastUs, (unsigned long)(c.count ? c.minUs : 0), (unsigned long)avg,
               (unsigned long)c.maxUs, (unsigned long)c.budgetUs, (unsigned long)c.overruns);
  }

  if (s_nCounters) {
    out.println("--- counters ---");
    for (uint8_t i = 0; i < s_nCounters; i++) out.printf("%-20s %lu\n", s_counters[i]->name, (unsigned long)s_counters[i]->value);
  }
  out.println("=====================================");
}

// ---------------------------------------------------------------------------
//  Synthetic load
// ---------------------------------------------------------------------------
static volatile uint8_t  s_ldPct = 0;
static volatile uint32_t s_ldEndMs = 0;
static volatile bool     s_ldRunning = false;

static void loadTask(void*) {
  Serial.printf("[load] started: %u %% for %lu ms\n", s_ldPct, (unsigned long)(s_ldEndMs - millis()));
  while ((int32_t)(s_ldEndMs - millis()) > 0) {
    int64_t t0 = esp_timer_get_time();
    int64_t busyUs = (int64_t)s_ldPct * 1000;      // pct % of a 100 ms period
    while (esp_timer_get_time() - t0 < busyUs) { __asm__ __volatile__("nop"); }
    vTaskDelay(pdMS_TO_TICKS(100 - s_ldPct));
  }
  Serial.println("[load] finished");
  s_ldRunning = false;
  vTaskDelete(NULL);
}

bool perfLoadStart(uint8_t pct, UBaseType_t prio, BaseType_t core, uint32_t seconds) {
  if (s_ldRunning || pct < 1 || pct > 95 || seconds < 1) return false;
  s_ldPct = pct;
  s_ldEndMs = millis() + seconds * 1000UL;
  s_ldRunning = true;
  if (xTaskCreatePinnedToCore(loadTask, "loadGen", 3072, NULL, prio, NULL, i2Core(core)) != pdPASS) {
    s_ldRunning = false;
    return false;
  }
  return true;
}
