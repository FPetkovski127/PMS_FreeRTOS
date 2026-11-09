#pragma once
#include "app_config.h"

// ---------------------------------------------------------------- types -----
enum PmsState : uint8_t { PMS_WARMUP, PMS_MEASURE, PMS_SLEEP };

struct PmsSample { float pm2_5, pm10, temp, humid; };       // calibrated values

struct TsMsg {                                              // FSM -> ThingSpeak task
  PmsSample d;
  float    laeq, lamax;                                     // sound (valid if hasSound)
  bool     hasSound;
  uint16_t samples;
};

// ----------------------------------------------------- shared state ---------
extern volatile PmsState g_pmsState;
extern volatile uint32_t g_stateStartMs, g_stateDurMs;      // for the "time left" on the display
extern volatile uint32_t g_pmsHeartbeat;                    // incremented every FSM loop (watchdog)
extern TaskHandle_t      g_pmsTask;
extern QueueHandle_t     g_tsQ;
extern volatile int      g_tsLastStatus;                    // last HTTP status (0 = nothing sent yet)
extern volatile uint32_t g_tsLastOkMs;                      // millis() of the last good upload (0 = never)

void        appStateInit();
void        appLatestSet(const PmsSample& s);
PmsSample   appLatestGet();
const char* pmsStateName(PmsState s);
void        hwWdtKick();                                    // pulse the external 555 watchdog

// ----------------------------------------------------- perf probes ----------
extern PerfProbe   pr_pmsPeriod;     // FSM wake-up lateness (scheduling jitter)
extern PerfProbe   pr_pmsBody;       // FSM loop body execution time
extern PerfProbe   pr_tsHttp;        // ThingSpeak HTTP round trip
extern PerfProbe   pr_oled;          // OLED frame
extern PerfCounter ct_tsDrop, ct_tsFail, ct_badFrame, ct_noSamples;

// -------------------------------------------- remote calibration (TS) -------
struct CalPending { bool valid; uint32_t version; I2Calib cal[4]; };
bool calGetPending(CalPending& out);
bool calAcceptPending();            // apply + store the pending set
void calRejectPending();
void calRequestFetch();             // ask the ThingSpeak task to check now

// ----------------------------------------------------- task entry points ----
void pmsTask(void*);
void tsTask(void*);
void displayTask(void*);
void ledTask(void*);
void hwWdtTask(void*);
void sensorConsoleInit();           // registers 'perf', 'slm', 'cal' commands
