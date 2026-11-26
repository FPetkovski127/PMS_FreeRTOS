// State machine of the sensor: WARMUP (fan on, data discarded) -> MEASURE (samples are
// calibrated and averaged, the average goes to the upload task) -> SLEEP (fan off).
// In fast mode SLEEP is skipped and MEASURE repeats. The sleep wait ends early when the
// task gets a notification (mode changed).
#include "app_state.h"
#include "pms5003t.h"
#include "sound_level.h"
#include <HardwareSerial.h>
#include <esp_task_wdt.h>

static HardwareSerial pmsSerial(2);

struct Acc { float pm25 = 0, pm10 = 0, t = 0, h = 0; uint16_t n = 0; };

static void sensorPower(bool on) { digitalWrite(PIN_PMS_SET, on ? HIGH : LOW); }

static void enterState(PmsState s, uint32_t durMs) {
  g_pmsState = s;
  g_stateStartMs = millis();
  g_stateDurMs = durMs;
  Serial.printf("[pms] -> %s (%lu s)\n", pmsStateName(s), (unsigned long)(durMs / 1000));
}

static void flushUart() { while (pmsSerial.available()) pmsSerial.read(); }

static void addSample(Acc& a, const PmsRaw& r) {
  I2Calib cal[4];
  i2cfgGetCal(cal);
  PmsSample s = { i2CalApply(cal[0], r.pm2_5), i2CalApply(cal[1], r.pm10),
                  i2CalApply(cal[2], r.temp),  i2CalApply(cal[3], r.humid) };
  if (s.pm2_5 < 0) s.pm2_5 = 0;
  if (s.pm10 < 0)  s.pm10 = 0;
  if (s.humid < 0) s.humid = 0;
  if (s.humid > 100) s.humid = 100;
  a.pm25 += s.pm2_5; a.pm10 += s.pm10; a.t += s.temp; a.h += s.humid; a.n++;
  appLatestSet(s);
  Serial.printf("[pms] sample %u: PM2.5 %.1f  PM10 %.1f  T %.1f  H %.1f\n", a.n, s.pm2_5, s.pm10, s.temp, s.humid);
}

static void finishWindow(const Acc& a) {
  if (a.n == 0) {
    perfCount(ct_noSamples);
    Serial.println("[pms] no valid frames in this window - check wiring / SET pin");
    return;
  }
  TsMsg m;
  m.d = { a.pm25 / a.n, a.pm10 / a.n, a.t / a.n, a.h / a.n };
  m.samples = a.n;
  m.hasSound = slmTakeWindow(&m.laeq, &m.lamax);
  if (!m.hasSound) { m.laeq = 0; m.lamax = 0; }
  Serial.printf("[pms] average of %u samples: PM2.5 %.1f PM10 %.1f T %.1f H %.1f  LAeq %.1f dBA\n",
                m.samples, m.d.pm2_5, m.d.pm10, m.d.temp, m.d.humid, m.laeq);
  if (xQueueSend(g_tsQ, &m, 0) != pdTRUE) {
    perfCount(ct_tsDrop);
    Serial.println("[pms] ThingSpeak queue full - average dropped");
  }
}

void pmsTask(void*) {
  pmsSerial.setRxBufferSize(512);                       // before begin()
  pmsSerial.begin(9600, SERIAL_8N1, PIN_PMS_RX, PIN_PMS_TX);
  i2WdtAddSelf();

  PmsParser parser;
  Acc acc;
  sensorPower(true);
  enterState(PMS_WARMUP, PMS_WARMUP_MS);

  for (;;) {
    // this wait is the loop delay and ends early when the settings change
    uint32_t t0 = perfNowUs();
    uint32_t notified = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(PMS_LOOP_MS));
    uint32_t t1 = perfNowUs();
    if (!notified) {
      uint32_t waited = t1 - t0, expected = PMS_LOOP_MS * 1000UL;
      perfRecord(&pr_pmsPeriod, waited > expected ? waited - expected : 0);   // how late the task woke up
    }

    g_pmsHeartbeat++;
    i2WdtFeed();

    const bool fast = i2cfgFastMode();
    const uint32_t sleepMs = (uint32_t)i2cfgSleepSec() * 1000UL;
    const uint32_t el = millis() - g_stateStartMs;

    if (g_pmsState != PMS_SLEEP) {
      PmsRaw r;
      while (pmsSerial.available()) {
        if (parser.feed((uint8_t)pmsSerial.read(), r) && g_pmsState == PMS_MEASURE) addSample(acc, r);
      }
      ct_badFrame.value = parser.badFrames;
    }

    switch (g_pmsState) {
      case PMS_WARMUP:
        if (el >= PMS_WARMUP_MS) {
          acc = Acc();
          enterState(PMS_MEASURE, fast ? PMS_FAST_WINDOW_MS : PMS_MEASURE_MS);
        }
        break;

      case PMS_MEASURE: {
        const uint32_t win = fast ? PMS_FAST_WINDOW_MS : PMS_MEASURE_MS;
        g_stateDurMs = win;
        if (el >= win) {
          finishWindow(acc);
          acc = Acc();
          if (fast) {
            enterState(PMS_MEASURE, PMS_FAST_WINDOW_MS);     // fast mode: no sleep
          } else {
            sensorPower(false);
            parser.reset();
            enterState(PMS_SLEEP, sleepMs);
          }
        }
        break;
      }

      case PMS_SLEEP:
        g_stateDurMs = sleepMs;
        if (fast || el >= sleepMs) {                         // fast mode ends the sleep
          sensorPower(true);
          parser.reset();
          flushUart();
          enterState(PMS_WARMUP, PMS_WARMUP_MS);
        }
        break;
    }

    perfRecord(&pr_pmsBody, perfNowUs() - t1);
  }
}
