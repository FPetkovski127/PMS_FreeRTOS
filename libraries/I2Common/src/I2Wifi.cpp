#include "I2Wifi.h"
#include "I2Settings.h"
#include "I2Task.h"
#include <WiFi.h>
#include <WiFiManager.h>
#include <ArduinoOTA.h>

volatile int8_t g_i2OtaPercent = -1;

static const uint32_t CONNECT_TIMEOUT_MS   = 20000;   // per attempt
static const uint32_t GIVE_UP_EARLY_MS     = 6000;    // wrong password / SSID not found
static const uint32_t BACKOFF_MS           = 5000;
static const uint32_t PORTAL_AFTER_DOWN_MS = 60000UL;   // link down this long -> open the portal
static const uint32_t PORTAL_GAP_MS        = 15UL * 60UL * 1000UL;
static const uint32_t PORTAL_TIMEOUT_S     = 180;
static const uint32_t STACK_RESET_AFTER_MS = 120000UL;
static const uint32_t RESTART_AFTER_MS     = 600000UL;
static const uint32_t SERVICE_STAY_MS      = 600000UL;

static volatile bool     s_onService = false;
static volatile bool     s_portalRequested = false;
static volatile uint32_t s_downMs = 0;
static void (*s_otaStartHook)() = nullptr;
static bool s_otaBegun = false;

bool i2wifiConnected()  { return WiFi.status() == WL_CONNECTED; }
bool i2wifiOnServiceNet() { return s_onService && i2wifiConnected(); }
uint32_t i2wifiDownMs() { return s_downMs; }
void i2wifiSetOtaStartHook(void (*fn)()) { s_otaStartHook = fn; }
void i2wifiRequestPortal() { s_portalRequested = true; }

void i2wifiPrintStatus(Print& out) {
  if (i2wifiConnected()) {
    out.printf("WiFi: connected to '%s'%s  IP %s  RSSI %d dBm\n", WiFi.SSID().c_str(),
               s_onService ? " (SERVICE net)" : "", WiFi.localIP().toString().c_str(), WiFi.RSSI());
  } else {
    out.printf("WiFi: DISCONNECTED for %lu s\n", (unsigned long)(s_downMs / 1000));
  }
}

// ---------------------------------------------------------------------------
static bool connectTo(const char* ssid, const char* pass) {
  Serial.printf("[wifi] connecting to '%s' ...\n", ssid);
  WiFi.disconnect(false, false);
  vTaskDelay(pdMS_TO_TICKS(100));
  WiFi.begin(ssid, (pass && pass[0]) ? pass : nullptr);
  uint32_t t0 = millis();
  for (;;) {
    wl_status_t st = WiFi.status();
    if (st == WL_CONNECTED) return true;
    uint32_t el = millis() - t0;
    if (el >= CONNECT_TIMEOUT_MS) break;
    if (el >= GIVE_UP_EARLY_MS && (st == WL_NO_SSID_AVAIL || st == WL_CONNECT_FAILED)) break;
    vTaskDelay(pdMS_TO_TICKS(250));
  }
  Serial.printf("[wifi] '%s' failed (status %d)\n", ssid, (int)WiFi.status());
  WiFi.disconnect(false, false);
  return false;
}

static void otaSetup() {
  const I2Defaults& d = i2cfgDefaults();
  ArduinoOTA.setHostname(d.hostname);
  if (d.otaPass && d.otaPass[0]) ArduinoOTA.setPassword(d.otaPass);
  ArduinoOTA.onStart([]() {
    g_i2OtaPercent = 0;
    Serial.println("[ota] start");
    if (s_otaStartHook) s_otaStartHook();
  });
  ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
    g_i2OtaPercent = (int8_t)((done * 100ULL) / total);
  });
  ArduinoOTA.onEnd([]() { g_i2OtaPercent = 100; Serial.println("[ota] finished - rebooting"); });
  ArduinoOTA.onError([](ota_error_t e) { g_i2OtaPercent = -2; Serial.printf("[ota] error %u\n", (unsigned)e); });
}

static void otaStart() {
  if (s_otaBegun) ArduinoOTA.end();
  ArduinoOTA.begin();
  s_otaBegun = true;
}

// ---------------------------------------------------------------------------
//  Config portal (WiFiManager). Blocks the supervisor task for up to 3 min;
//  all other tasks keep running.
// ---------------------------------------------------------------------------
static WiFiManager* s_wm = nullptr;
static bool   s_pGot = false;
static String s_pCode, s_pCh, s_pKey, s_pMode;

static void portalSaveCb() {                       // runs when the portal form is submitted
  if (!s_wm) return;
  s_pCode = s_wm->server->arg("accesscode");
  s_pCh   = s_wm->server->arg("channelnum");
  s_pKey  = s_wm->server->arg("channelkey");
  s_pMode = s_wm->server->hasArg("channelmode") ? "T" : "";
  s_pGot = true;
}

static bool runPortal() {
  const I2Defaults& d = i2cfgDefaults();
  const bool sensor = (d.role == I2_ROLE_SENSOR);
  I2Settings c;
  i2cfgSnapshot(c);
  String chStr = String((unsigned long)c.channelId);

  Serial.printf("[portal] opening access point '%s' for %lu s\n", d.apSsid, (unsigned long)PORTAL_TIMEOUT_S);
  WiFiManager wm;
  s_wm = &wm;
  s_pGot = false;

  WiFiManagerParameter pCode("accesscode", "Access code (required)", "", 16, "type='password'");
  WiFiManagerParameter pCh("channelnum", "ThingSpeak channel ID (6-8 digits)", chStr.c_str(), 8);
  WiFiManagerParameter pKey("channelkey", sensor ? "Channel WRITE API key (16 chars)" : "Channel READ API key (16 chars)",
                            sensor ? c.writeKey : c.readKey, 16);
  WiFiManagerParameter pMode("channelmode", "Fast mode", "T", 1, "type=\"checkbox\"");
  wm.addParameter(&pCode);
  wm.addParameter(&pCh);
  wm.addParameter(&pKey);
  if (sensor) wm.addParameter(&pMode);

  wm.setSaveParamsCallback(portalSaveCb);
  wm.setConfigPortalTimeout(PORTAL_TIMEOUT_S);
  wm.setConnectTimeout(20);
  wm.setBreakAfterConfig(true);
  bool connected = wm.startConfigPortal(d.apSsid, d.apPass);
  s_wm = nullptr;
  WiFi.softAPdisconnect(true);                     // make sure the AP is gone
  WiFi.mode(WIFI_STA);

  bool saved = false;
  if (s_pGot) {
    if (!i2cfgCheckAccessCode(s_pCode.c_str())) {
      Serial.println("[portal] WRONG access code - nothing saved");
      if (WiFi.status() == WL_CONNECTED) WiFi.disconnect(false, false);
      connected = false;
    } else if (connected) {
      I2Settings n;
      i2cfgSnapshot(n);
      i2Copy(n.ssid, sizeof(n.ssid), WiFi.SSID().c_str());
      i2Copy(n.wifiPass, sizeof(n.wifiPass), WiFi.psk().c_str());
      uint32_t id;
      if (i2ValidChannelId(s_pCh, &id)) n.channelId = id;
      if (i2ValidApiKey(s_pKey)) i2Copy(sensor ? n.writeKey : n.readKey, 17, s_pKey.c_str());
      if (sensor) n.fastMode = (s_pMode == "T") ? 1 : 0;
      saved = i2cfgCommit(n);
      Serial.println(saved ? "[portal] settings saved" : "[portal] SAVE FAILED");
    }
  }
  s_pCode = ""; s_pKey = "";
  if (connected) s_onService = false;
  return connected;
}

static void wifiTask(void*) {
  bool wasConnected = false;
  bool stackReset = false;
  uint32_t downSince = millis();
  uint32_t connectedAt = 0;
  uint32_t otaErrAt = 0;
  uint32_t lastPortal = 0;                               // 0 = never opened since boot

  for (;;) {
    if (WiFi.status() == WL_CONNECTED) {
      if (!wasConnected) {
        wasConnected = true;
        stackReset = false;
        s_downMs = 0;
        connectedAt = millis();
        Serial.printf("[wifi] connected: %s  IP %s  RSSI %d%s\n", WiFi.SSID().c_str(),
                      WiFi.localIP().toString().c_str(), WiFi.RSSI(), s_onService ? "  (SERVICE net)" : "");
        otaStart();
      }
      ArduinoOTA.handle();

      if (s_portalRequested) {                           // console command 'portal'
        s_portalRequested = false;
        runPortal();
        lastPortal = millis();
        downSince = millis();                            // outage clock restarts after the portal
        wasConnected = false;                            // re-run the connected branch (OTA restart)
        continue;
      }
      if (g_i2OtaPercent == -2) {                        // show OTA error for 5 s only
        if (!otaErrAt) otaErrAt = millis();
        if (millis() - otaErrAt > 5000) { g_i2OtaPercent = -1; otaErrAt = 0; }
      }

      I2Settings c;
      i2cfgSnapshot(c);
      if (s_onService && c.ssid[0] && g_i2OtaPercent < 0 && millis() - connectedAt > SERVICE_STAY_MS) {
        Serial.println("[wifi] leaving service net - retrying configured network");
        WiFi.disconnect(false, false);
      }
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    // ----- not connected -----
    if (wasConnected) {
      wasConnected = false;
      downSince = millis();
      Serial.println("[wifi] link lost");
    }
    I2Settings c;
    i2cfgSnapshot(c);
    const I2Defaults& d = i2cfgDefaults();
    bool haveSvc = d.svcSsid && d.svcSsid[0];
    bool haveAny = c.ssid[0] || haveSvc;

    s_downMs = millis() - downSince;
    if (haveAny && s_downMs > RESTART_AFTER_MS) {
      Serial.println("[wifi] down too long - restarting ESP32");
      vTaskDelay(pdMS_TO_TICKS(500));
      ESP.restart();
    }
    if (s_downMs > STACK_RESET_AFTER_MS && !stackReset) {
      Serial.println("[wifi] resetting WiFi stack");
      WiFi.disconnect(true, false);
      vTaskDelay(pdMS_TO_TICKS(500));
      WiFi.mode(WIFI_STA);
      stackReset = true;
    }

    bool ok = false;
    if (c.ssid[0]) {
      ok = connectTo(c.ssid, c.wifiPass);
      if (ok) s_onService = false;
    }
    if (!ok && haveSvc) {
      ok = connectTo(d.svcSsid, d.svcPass);
      if (ok) s_onService = true;
    }
    if (!ok) {
      bool portalDue = s_portalRequested ||
                       (s_downMs > PORTAL_AFTER_DOWN_MS && (lastPortal == 0 || millis() - lastPortal > PORTAL_GAP_MS));
      if (portalDue) {
        s_portalRequested = false;
        runPortal();
        lastPortal = millis();
      } else {
        vTaskDelay(pdMS_TO_TICKS(BACKOFF_MS));
      }
    }
  }
}

void i2wifiBegin() {
  const I2Defaults& d = i2cfgDefaults();
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);          // the supervisor owns reconnection
  WiFi.setHostname(d.hostname);
  WiFi.mode(WIFI_STA);                   // normal operation is STA only; the AP exists only inside runPortal()
  WiFi.setSleep(false);                  // more reliable link, slightly more power
  otaSetup();
  i2CreateTask(wifiTask, "wifiTask", 6144, NULL, 2, NULL, I2_CORE_PRO);
}
