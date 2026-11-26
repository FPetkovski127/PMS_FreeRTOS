#pragma once
#include "app_config.h"

// types
enum PmsState : uint8_t { PMS_WARMUP, PMS_MEASURE, PMS_SLEEP };

struct PmsSample { float pm2_5, pm10, temp, humid; };       // calibrated values

struct TsMsg {                                              // state machine -> upload task
  PmsSample d;
  float    laeq, lamax;                                     // only valid if hasSound
  bool     hasSound;
  uint16_t samples;
};

// shared state
extern volatile PmsState g_pmsState;
extern volatile uint32_t g_stateStartMs, g_stateDurMs;      // used for the time left on the display
extern volatile uint32_t g_pmsHeartbeat;                    // counts state machine loops, shown by 'status'
extern TaskHandle_t      g_pmsTask;
extern QueueHandle_t     g_tsQ;
extern volatile int      g_tsLastStatus;                    // last HTTP status, 0 = nothing sent yet
extern volatile uint32_t g_tsLastOkMs;                      // millis() of the last good upload, 0 = never

void        appStateInit();
void        appLatestSet(const PmsSample& s);
PmsSample   appLatestGet();
const char* pmsStateName(PmsState s);

// timing probes
extern PerfProbe   pr_pmsPeriod;     // wake-up lateness of the state machine
extern PerfProbe   pr_pmsBody;       // execution time of one loop
extern PerfProbe   pr_tsHttp;        // ThingSpeak HTTP request
extern PerfProbe   pr_oled;          // OLED frame
extern PerfCounter ct_tsDrop, ct_tsFail, ct_badFrame, ct_noSamples;

// remote calibration
struct CalPending { bool valid; uint32_t version; I2Calib cal[4]; };
bool calGetPending(CalPending& out);
bool calAcceptPending();            // apply and store the pending set
void calRejectPending();
void calRequestFetch();             // check the calibration channel now

// task functions
void pmsTask(void*);
void tsTask(void*);
void displayTask(void*);
void ledTask(void*);
void sensorConsoleInit();           // adds the 'perf', 'slm' and 'cal' commands
