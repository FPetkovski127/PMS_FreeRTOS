// OLED status display (used on the bench, the matrix unit is the final display)
#include "app_state.h"
#include "sound_level.h"

#if ENABLE_OLED
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

static Adafruit_SSD1306 display(128, 64, &Wire, -1);

static void drawOta(int pct) {
  display.clearDisplay();
  display.setCursor(0, 0);
  if (pct == -2) { display.print("OTA ERROR"); }
  else {
    display.printf("OTA update %d%%", pct);
    display.drawRect(7, 30, 114, 10, SSD1306_WHITE);
    display.fillRect(8, 31, (112 * pct) / 100, 8, SSD1306_WHITE);
  }
  display.display();
}

void displayTask(void*) {
  Wire.begin();
  Wire.setClock(400000);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("[oled] init failed - display task idles");
    for (;;) vTaskDelay(portMAX_DELAY);
  }
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  for (;;) {
    uint32_t t0 = perfNowUs();
    int ota = g_i2OtaPercent;
    if (ota != -1) {
      drawOta(ota);
    } else {
      PmsSample s = appLatestGet();
      uint32_t el = millis() - g_stateStartMs;
      uint32_t left = (g_stateDurMs > el) ? (g_stateDurMs - el) / 1000 : 0;

      display.clearDisplay();
      display.setCursor(0, 0);
      display.printf("%s %lus", pmsStateName(g_pmsState), (unsigned long)left);
      display.setCursor(80, 0);
      display.print(i2cfgFastMode() ? "FAST" : "NORMAL");
      display.setCursor(0, 12);
      display.printf("PM2.5:%.0f PM10:%.0f", s.pm2_5, s.pm10);
      display.setCursor(0, 24);
      display.printf("T:%.1fC H:%.0f%%", s.temp, s.humid);
      display.setCursor(0, 36);
      SlmStatus sl = slmStatus();
      if (!sl.running)      display.print("Noise: off");
      else if (!sl.valid)   display.print("Noise: ...");
      else if (sl.overload) display.print("Noise: OVERLOAD");
      else if (sl.low)      display.printf("Noise: <%.0f dBA", sl.leq1s);
      else                  display.printf("Noise: %.1f dBA", sl.leq1s);
      display.setCursor(0, 48);
      if (i2wifiConnected()) display.printf("WiFi%s TS:%d", i2wifiOnServiceNet() ? "(svc)" : "", (int)g_tsLastStatus);
      else                   display.printf("WiFi down %lus", (unsigned long)(i2wifiDownMs() / 1000));
      display.display();
    }
    perfRecord(&pr_oled, perfNowUs() - t0);
    vTaskDelay(pdMS_TO_TICKS(250));
  }
}
#else
void displayTask(void*) { vTaskDelete(NULL); }
#endif
