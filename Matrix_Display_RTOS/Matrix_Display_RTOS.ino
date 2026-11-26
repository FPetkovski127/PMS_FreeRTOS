// Matrix display unit (FreeRTOS)
//
// Tasks: ts_read (2) reads the values from ThingSpeak every 30 s, display (3) scrolls the text,
// wifiTask (2) and console (1) come from the I2Common library.
// The WiFi supervisor in the library reconnects (configured network, then service network,
// WiFi stack reset after 2 min, restart after 10 min). This sketch also restarts the unit
// when WiFi is up but no ThingSpeak read worked for NO_DATA_RESTART_MS.
#include "app_config.h"
#include <WiFi.h>
#include <ThingSpeak.h>
#include <Adafruit_GFX.h>
#include <Adafruit_NeoMatrix.h>
#include <Adafruit_NeoPixel.h>

Adafruit_NeoMatrix matrix = Adafruit_NeoMatrix(
  MATRIX_W, MATRIX_H, PIN_DIN,
  NEO_MATRIX_TOP + NEO_MATRIX_LEFT + NEO_MATRIX_COLUMNS + NEO_MATRIX_ZIGZAG,
  NEO_GRB + NEO_KHZ800);   // origin top left, columns, zig-zag wiring

#define WHITE  matrix.Color(255, 255, 255)
#define RED    matrix.Color(255, 0, 0)
#define GREEN  matrix.Color(0, 255, 0)
#define BLUE   matrix.Color(0, 0, 255)
#define YELLOW matrix.Color(255, 255, 0)
#define ORANGE matrix.Color(255, 165, 0)
#define LIME   matrix.Color(50, 205, 50)

// data
struct MatrixData { int temp, humid, pm2_5, pm10, noise; };   // noise < 0 = not available

static const I2Defaults DEFAULTS = {
  I2_ROLE_MATRIX, FW_VERSION, "Matrix-Display",
  I2_DEFAULT_WIFI_SSID, I2_DEFAULT_WIFI_PASS,
  DEFAULT_CHANNEL_ID, "", DEFAULT_READ_KEY,
  I2_DEFAULT_ACCESS_CODE,
  I2_SERVICE_WIFI_SSID, I2_SERVICE_WIFI_PASS, I2_OTA_PASSWORD,
  I2_AP_SSID, I2_AP_PASSWORD,
  240,
  {1.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 0.0f}
};

static WiFiClient   s_client;
static QueueHandle_t s_dataQ = nullptr;                     // length 1, holds the newest reading
static volatile uint32_t s_lastOkMs = 0;
static volatile int      s_lastStatus = 0;

static PerfProbe   pr_tsRead = PERF_PROBE_INIT("ts_read_http", 0);
static PerfProbe   pr_frame  = PERF_PROBE_INIT("matrix_frame", SCROLL_DELAY_MS * 1000UL);
static PerfCounter ct_readFail = PERF_COUNTER_INIT("ts_read_failed");

// colours
static uint16_t colorTemp(int t)  { return t < 10 ? BLUE : t < 20 ? YELLOW : t < 30 ? ORANGE : RED; }
static uint16_t colorHumid(int h) { return h < 30 ? LIME : h < 60 ? YELLOW : ORANGE; }
static uint16_t colorPM25(int p)  { return p < 35 ? GREEN : p < 53 ? YELLOW : p < 70 ? ORANGE : RED; }
static uint16_t colorPM10(int p)  { return p < 50 ? GREEN : p < 75 ? YELLOW : p < 100 ? ORANGE : RED; }
static uint16_t colorNoise(int n) { return n < 55 ? GREEN : n < 70 ? YELLOW : n < 85 ? ORANGE : RED; }

// ThingSpeak task
static void tsReadTask(void*) {
  ThingSpeak.begin(s_client);
  s_lastOkMs = millis();

  for (;;) {
    if (!i2wifiConnected()) { vTaskDelay(pdMS_TO_TICKS(1000)); continue; }

    I2Settings c;
    i2cfgSnapshot(c);
    uint32_t t0 = perfNowUs();
    int st = c.readKey[0] ? ThingSpeak.readMultipleFields(c.channelId, c.readKey)
                          : ThingSpeak.readMultipleFields(c.channelId);
    perfRecord(&pr_tsRead, perfNowUs() - t0);
    s_lastStatus = st;

    uint32_t waitMs = TS_RETRY_MS;
    if (st == 200) {
      MatrixData d;
      d.pm2_5 = lroundf(ThingSpeak.getFieldAsFloat(1));
      d.pm10  = lroundf(ThingSpeak.getFieldAsFloat(2));
      d.temp  = lroundf(ThingSpeak.getFieldAsFloat(3));
      d.humid = lroundf(ThingSpeak.getFieldAsFloat(4));
      String n = ThingSpeak.getFieldAsString(5);
      d.noise = n.length() ? lroundf(n.toFloat()) : -1;
      Serial.printf("[ts] T=%d H=%d PM2.5=%d PM10=%d Noise=%d\n", d.temp, d.humid, d.pm2_5, d.pm10, d.noise);
      xQueueOverwrite(s_dataQ, &d);
      s_lastOkMs = millis();
      waitMs = TS_READ_MS;
    } else {
      perfCount(ct_readFail);
      Serial.printf("[ts] read failed, HTTP %d\n", st);
      if (millis() - s_lastOkMs > NO_DATA_RESTART_MS) {
        Serial.println("[ts] no data for too long - restarting");
        vTaskDelay(pdMS_TO_TICKS(500));
        ESP.restart();
      }
    }
    vTaskDelay(pdMS_TO_TICKS(waitMs));
  }
}

// display task
struct Seg { String text; uint16_t color; };

static int buildSegments(const MatrixData& d, Seg* s) {
  int n = 0;
  s[n++] = { "KHIE @ FEIT Vozduh: ", WHITE };
  s[n++] = { "T:" + String(d.temp) + "C ", colorTemp(d.temp) };
  s[n++] = { "H:" + String(d.humid) + "% ", colorHumid(d.humid) };
  s[n++] = { "PM2.5:" + String(d.pm2_5) + " ", colorPM25(d.pm2_5) };
  s[n++] = { "PM10:" + String(d.pm10) + " ", colorPM10(d.pm10) };
  if (d.noise >= 0) s[n++] = { "N:" + String(d.noise) + "dB ", colorNoise(d.noise) };
  s[n++] = { "    ", WHITE };                                   // gap before the message restarts
  return n;
}

static void displayTask(void*) {
  MatrixData latest = {0, 0, 0, 0, -1}, fresh = {0, 0, 0, 0, -1};
  bool pendingNew = false;    // new data arrived - shown only after the running message has scrolled out
  bool haveData = false;      // nothing real to show until the first ThingSpeak read succeeded

  matrix.begin();
  matrix.setTextWrap(false);
  matrix.setBrightness(MATRIX_BRIGHTNESS);
  matrix.setFont();           // default 6x8 font: one character = 6 pixels
  int offset = MATRIX_W;
  i2WdtAddSelf();             // internal watchdog: a frozen display task resets the unit

  for (;;) {
    uint32_t t0 = perfNowUs();
    i2WdtFeed();
    matrix.fillScreen(0);

    if (!i2wifiConnected()) {                                  // link down -> "Connecting.."
      matrix.setTextColor(WHITE);
      matrix.setCursor(offset, 0);
      matrix.print("Connecting.. ");
      matrix.show();
      if (--offset < -100) offset = MATRIX_W;
      perfRecord(&pr_frame, perfNowUs() - t0);
      vTaskDelay(pdMS_TO_TICKS(CONNECTING_DELAY_MS));
      continue;
    }

    if (xQueueReceive(s_dataQ, &fresh, 0) == pdTRUE) {
      if (!haveData) { latest = fresh; haveData = true; }      // first reading: show at once
      else pendingNew = true;
    }
    if (!haveData) {                                           // connected, waiting for the first reading
      matrix.show();
      perfRecord(&pr_frame, perfNowUs() - t0);
      vTaskDelay(pdMS_TO_TICKS(CONNECTING_DELAY_MS));
      continue;
    }

    Seg segs[7];
    int n = buildSegments(latest, segs);
    int msgW = 0;
    matrix.setCursor(offset, 0);
    for (int i = 0; i < n; i++) {
      matrix.setTextColor(segs[i].color);
      matrix.print(segs[i].text);
      msgW += segs[i].text.length() * 6;
    }
    matrix.show();
    perfRecord(&pr_frame, perfNowUs() - t0);
    offset--;

    if (offset < -msgW) {                                      // message fully scrolled out
      offset = MATRIX_W;
      if (pendingNew) { latest = fresh; pendingNew = false; }
    }
    vTaskDelay(pdMS_TO_TICKS(SCROLL_DELAY_MS));
  }
}

// console
static bool matrixExt(const String& cmd, const String& args) {
  if (cmd != "perf") return false;
  if (args.length() == 0)    perfReport(Serial);
  else if (args == "reset")  { perfReset(); Serial.println("perf statistics cleared"); }
  else if (args == "cpu on") { perfCpuEnable(true); Serial.println("CPU load sampling ON"); }
  else if (args == "cpu off"){ perfCpuEnable(false); Serial.println("CPU load sampling OFF"); }
  else Serial.println("perf [reset | cpu on|off]");
  return true;
}

static void matrixStatus() {
  Serial.printf("ThingSpeak: last HTTP %d, last good read %lu s ago\n", (int)s_lastStatus,
                (unsigned long)((millis() - s_lastOkMs) / 1000));
}

// setup / loop
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.printf("\n=== Matrix display unit, firmware %s ===\n", FW_VERSION);

  i2WdtInit(WDT_TIMEOUT_S);
  i2cfgBegin(DEFAULTS);
  s_dataQ = xQueueCreate(1, sizeof(MatrixData));
  perfAddProbe(&pr_tsRead);
  perfAddProbe(&pr_frame);
  perfAddCounter(&ct_readFail);
  perfBegin();

  i2wifiBegin();
  i2CreateTask(tsReadTask,  "ts_read", 6144, NULL, PRIO_TS,      NULL, CORE_NET);
  i2CreateTask(displayTask, "display", 6144, NULL, PRIO_DISPLAY, NULL, CORE_DISPLAY);
  i2consoleBegin(matrixExt, "Matrix commands:\n  perf [reset | cpu on|off]\n", matrixStatus, NULL);
  Serial.println("Setup done");
}

void loop() {
  vTaskDelay(portMAX_DELAY);
}
