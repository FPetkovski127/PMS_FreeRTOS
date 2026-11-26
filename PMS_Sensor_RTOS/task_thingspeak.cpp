// ThingSpeak task: uploads the averages from the queue (with retries) and checks the
// calibration channel (field 1 = "version,g25,o25,g10,o10,gT,oT,gH,oH").
// Data channel fields: 1 PM2.5, 2 PM10, 3 temperature, 4 humidity, 5 LAeq, 6 LAmax.
#include "app_state.h"
#include <WiFi.h>
#include <ThingSpeak.h>

static WiFiClient s_client;
static uint32_t   s_lastWriteMs = 0;
static volatile bool s_fetchNow = false;

// remote calibration that has not been accepted yet
static CalPending s_pending = {false, 0, {{1, 0}, {1, 0}, {1, 0}, {1, 0}}};
static uint32_t   s_rejectedVer = 0;
static portMUX_TYPE s_calMux = portMUX_INITIALIZER_UNLOCKED;

bool calGetPending(CalPending& out) {
  portENTER_CRITICAL(&s_calMux);
  out = s_pending;
  portEXIT_CRITICAL(&s_calMux);
  return out.valid;
}

bool calAcceptPending() {
  CalPending p;
  if (!calGetPending(p)) return false;
  bool ok = i2cfgApplyCal(p.version, p.cal);
  if (ok) {
    portENTER_CRITICAL(&s_calMux);
    s_pending.valid = false;
    portEXIT_CRITICAL(&s_calMux);
  }
  return ok;
}

void calRejectPending() {
  portENTER_CRITICAL(&s_calMux);
  s_rejectedVer = s_pending.version;
  s_pending.valid = false;
  portEXIT_CRITICAL(&s_calMux);
}

void calRequestFetch() { s_fetchNow = true; }

static bool uploadOnce(const TsMsg& m, const I2Settings& c) {
  ThingSpeak.setField(1, m.d.pm2_5);
  ThingSpeak.setField(2, m.d.pm10);
  ThingSpeak.setField(3, m.d.temp);
  ThingSpeak.setField(4, m.d.humid);
  if (m.hasSound) {
    ThingSpeak.setField(5, m.laeq);
    ThingSpeak.setField(6, m.lamax);
  }
  uint32_t t0 = perfNowUs();
  int st = ThingSpeak.writeFields(c.channelId, c.writeKey);
  perfRecord(&pr_tsHttp, perfNowUs() - t0);
  s_lastWriteMs = millis();
  g_tsLastStatus = st;
  if (st == 200) { g_tsLastOkMs = millis(); return true; }
  Serial.printf("[ts] upload failed, HTTP %d\n", st);
  return false;
}

static void sendWithRetry(const TsMsg& m) {
  for (int attempt = 0; attempt < TS_RETRIES; attempt++) {
    while (!i2wifiConnected()) vTaskDelay(pdMS_TO_TICKS(1000));       // wait for the link
    while (millis() - s_lastWriteMs < TS_MIN_GAP_MS) vTaskDelay(pdMS_TO_TICKS(500));
    I2Settings c;
    i2cfgSnapshot(c);
    if (uploadOnce(m, c)) { Serial.println("[ts] upload OK"); return; }
    perfCount(ct_tsFail);
    vTaskDelay(pdMS_TO_TICKS(TS_RETRY_WAIT_MS));
  }
  Serial.println("[ts] giving up on this average");
}

// Returns true when the check itself succeeded.
static bool checkRemoteCalibration() {
  I2Settings c;
  i2cfgSnapshot(c);
  if (c.cfgChannelId == 0) return true;                               // feature not configured

  String s = c.cfgReadKey[0] ? ThingSpeak.readStringField(c.cfgChannelId, 1, c.cfgReadKey)
                             : ThingSpeak.readStringField(c.cfgChannelId, 1);
  int st = ThingSpeak.getLastReadStatus();
  if (st != 200) { Serial.printf("[cal] calibration channel read failed, HTTP %d\n", st); return false; }

  uint32_t ver;
  I2Calib cal[4];
  if (!i2CalParseCsv(s.c_str(), &ver, cal)) {
    Serial.printf("[cal] calibration field not valid: '%s'\n", s.c_str());
    return false;
  }
  if (ver == c.calVersion) { Serial.printf("[cal] up to date (version %lu)\n", (unsigned long)ver); return true; }
  if (ver < c.calVersion)  { Serial.printf("[cal] remote version %lu is OLDER than local %lu - ignored\n", (unsigned long)ver, (unsigned long)c.calVersion); return true; }
  if (ver == s_rejectedVer) return true;

  if (c.calAuto) {
    if (i2cfgApplyCal(ver, cal)) Serial.printf("[cal] remote calibration version %lu applied automatically\n", (unsigned long)ver);
    return true;
  }
  portENTER_CRITICAL(&s_calMux);
  s_pending.valid = true;
  s_pending.version = ver;
  for (int i = 0; i < 4; i++) s_pending.cal[i] = cal[i];
  portEXIT_CRITICAL(&s_calMux);
  Serial.printf("[cal] NEW calibration version %lu available (local %lu). Type 'cal show', then 'cal accept' or 'cal reject'.\n",
                (unsigned long)ver, (unsigned long)c.calVersion);
  return true;
}

void tsTask(void*) {
  ThingSpeak.begin(s_client);
  uint32_t nextCal = millis() + CAL_FIRST_CHECK_MS;

  for (;;) {
    TsMsg m;
    if (xQueueReceive(g_tsQ, &m, pdMS_TO_TICKS(1000)) == pdTRUE) sendWithRetry(m);

    bool due = s_fetchNow || (int32_t)(millis() - nextCal) >= 0;
    if (due && i2wifiConnected()) {
      s_fetchNow = false;
      nextCal = millis() + (checkRemoteCalibration() ? CAL_CHECK_PERIOD_MS : CAL_RETRY_MS);
    }
  }
}
