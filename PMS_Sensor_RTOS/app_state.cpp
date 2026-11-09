#include "app_state.h"

volatile PmsState g_pmsState = PMS_WARMUP;
volatile uint32_t g_stateStartMs = 0, g_stateDurMs = 0;
volatile uint32_t g_pmsHeartbeat = 0;
TaskHandle_t      g_pmsTask = nullptr;
QueueHandle_t     g_tsQ = nullptr;
volatile int      g_tsLastStatus = 0;
volatile uint32_t g_tsLastOkMs = 0;

static SemaphoreHandle_t s_latestMtx = nullptr;
static PmsSample         s_latest = {0, 0, 0, 0};

PerfProbe pr_pmsPeriod = PERF_PROBE_INIT("pms_wake_late", PMS_LOOP_MS * 1000UL);   // late > 1 period = bad
PerfProbe pr_pmsBody   = PERF_PROBE_INIT("pms_loop_body", PMS_LOOP_MS * 1000UL);
PerfProbe pr_tsHttp    = PERF_PROBE_INIT("ts_http", 0);
PerfProbe pr_oled      = PERF_PROBE_INIT("oled_frame", 250000UL);
PerfCounter ct_tsDrop    = PERF_COUNTER_INIT("ts_queue_dropped");
PerfCounter ct_tsFail    = PERF_COUNTER_INIT("ts_upload_failed");
PerfCounter ct_badFrame  = PERF_COUNTER_INIT("pms_bad_frames");
PerfCounter ct_noSamples = PERF_COUNTER_INIT("pms_empty_windows");

void appStateInit() {
  s_latestMtx = xSemaphoreCreateMutex();
  g_tsQ = xQueueCreate(PMS_QUEUE_LEN, sizeof(TsMsg));
  perfAddProbe(&pr_pmsPeriod);
  perfAddProbe(&pr_pmsBody);
  perfAddProbe(&pr_tsHttp);
  perfAddProbe(&pr_oled);
  perfAddCounter(&ct_tsDrop);
  perfAddCounter(&ct_tsFail);
  perfAddCounter(&ct_badFrame);
  perfAddCounter(&ct_noSamples);
}

void appLatestSet(const PmsSample& s) {
  if (xSemaphoreTake(s_latestMtx, pdMS_TO_TICKS(20)) == pdTRUE) {
    s_latest = s;
    xSemaphoreGive(s_latestMtx);
  }
}

PmsSample appLatestGet() {
  PmsSample r = {0, 0, 0, 0};
  if (xSemaphoreTake(s_latestMtx, pdMS_TO_TICKS(20)) == pdTRUE) {
    r = s_latest;
    xSemaphoreGive(s_latestMtx);
  }
  return r;
}

const char* pmsStateName(PmsState s) {
  switch (s) {
    case PMS_WARMUP:  return "WARMUP";
    case PMS_MEASURE: return "MEASURE";
    case PMS_SLEEP:   return "SLEEP";
  }
  return "?";
}

// Pulses the external 555 watchdog input. Also called right before every flash
// write (settings) so the 555 cannot time out in the middle of a write.
void hwWdtKick() {
#if PIN_HW_WDT_KICK >= 0
  digitalWrite(PIN_HW_WDT_KICK, HW_WDT_ACTIVE_LEVEL);
  vTaskDelay(pdMS_TO_TICKS(HW_WDT_PULSE_MS));
  digitalWrite(PIN_HW_WDT_KICK, !HW_WDT_ACTIVE_LEVEL);
#endif
}
