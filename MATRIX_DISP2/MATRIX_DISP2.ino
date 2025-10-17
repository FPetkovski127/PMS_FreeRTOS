
#include <Arduino.h>
#include <WiFi.h>
#include <ThingSpeak.h>
#include <Adafruit_GFX.h>
#include <Adafruit_NeoMatrix.h>
#include <Adafruit_NeoPixel.h>

#define DIN          5
#define MATRIX_W     16     // sirina na matrix 16 vo ovoj slucaj - vo proektot 64 
#define MATRIX_H     8     // visina 8

Adafruit_NeoMatrix matrix = Adafruit_NeoMatrix(
  MATRIX_W, MATRIX_H, DIN,
  NEO_MATRIX_TOP + NEO_MATRIX_LEFT + NEO_MATRIX_COLUMNS + NEO_MATRIX_ZIGZAG,
  NEO_GRB + NEO_KHZ800); // shirina=16, visina=8, DIN -> pin 5, Pocnuvajki od gore levo, dvizejki se niz koloni - taka sto sekoja nova kolona se cita so obraten redosled (ZIGZAG)
  


#define WHITE matrix.Color(255,255,255)
#define RED   matrix.Color(255,0,0)
#define GREEN matrix.Color(0,255,0)
#define BLUE  matrix.Color(0,0,255)
#define YELLOW matrix.Color(255,255,0)
#define ORANGE matrix.Color(255,165,0)
#define LIME matrix.Color(50,205,50)

// WiFi / ThingSpeak
const char ssid[] = "StuHo-WiFi";
const char pass[] = "Only4StuHos";
WiFiClient client;
unsigned long ChannelID = 3095045;
const char *ReadKey = "JM9QEV7SW8RKWRQZ";

//
// RTOS objects
//
SemaphoreHandle_t dataMutex = NULL;
QueueHandle_t tsQueue = NULL;          
TaskHandle_t displayTaskHandle = NULL; // ke ni posluzi za notifikacii 
// queue snap
typedef struct {
  int temp;
  int humid;
  int pm2_5;
  int pm10;
  unsigned long stamp;
} pms_data;

static const TickType_t SCROLL_DELAY_MS = 70; // zavisi od sirinata na displejot sto prikazuvame - za pomali displei podobro e pogolem delay - za poubavo da se vidi porakata
static const TickType_t TS_READ = 30000; // interval za citanje od ThingSpeak = 30sa

// === WiFi maintenance task ===
void WiFiTask(void *param) {
  (void) param;
  while (true) {
    if (WiFi.status() != WL_CONNECTED) {
      Serial.printf("WiFi: connecting to %s\n", ssid);
      WiFi.begin(ssid, pass);
      unsigned long t0 = millis();
      // wait up to 10s to connect
      while (WiFi.status() != WL_CONNECTED && (millis()-t0) < 10000) {
        vTaskDelay(pdMS_TO_TICKS(500));
      }
      if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("WiFi connected: %s\n", WiFi.localIP().toString().c_str());
      } else {
        Serial.println("WiFi connect attempt failed, will retry...");
      }
    }
    vTaskDelay(pdMS_TO_TICKS(5000)); // proveri status na WiFi sekoi 5 sekundi 
  }
}


// Cita od ThingSpeak - prakja podatoci vo queue - prakja notifikacija kon displejot dokolku ima nova informacija procitano od TS
void ThingSpeakTask(void *param) {
  (void) param;
  pms_data snap;
  while (true) {
    if (WiFi.status() == WL_CONNECTED) {
      snap.temp  = ThingSpeak.readIntField(ChannelID, 3, ReadKey);
      snap.humid = ThingSpeak.readIntField(ChannelID, 4, ReadKey);
      snap.pm2_5  = ThingSpeak.readIntField(ChannelID, 1, ReadKey);
      snap.pm10  = ThingSpeak.readIntField(ChannelID, 2, ReadKey);
      snap.stamp = millis();

      long status = ThingSpeak.getLastReadStatus();
      if (status == 200) {
        Serial.printf("TS read ok: T=%d H=%d PM2.5=%d PM10=%d\n", snap.temp, snap.humid, snap.pm2_5, snap.pm10);
        // prati na queue
        if (xQueueSend(tsQueue, &snap, pdMS_TO_TICKS(200)) != pdTRUE) { //prati na queue - ke ima status pdTRUE - dokolku queue e poln - printaj poraka
          Serial.println("TS queue full");
        } else {
          if (displayTaskHandle) xTaskNotifyGive(displayTaskHandle); // koga ke dobieme novi podatoci od TS osven sto prakjame na queue - prakjame i notifikacija kon displejot za instantna notifikacija
        }
      } else {
        Serial.printf("ThingSpeak error %ld\n", status);
      }
    } else {
      Serial.println("WiFi not connected");
    }

    vTaskDelay(pdMS_TO_TICKS(TS_READ));
  }
}

// === Display task ===
// takes pms_data from queue and scrolls composed message across the matrix.
// uses xTaskNotify to get immediate refresh on new data (and continues scrolling otherwise)
void DisplayTask(void *param) {
  (void) param;
  // text buffer and measurements
  String bigText = "";
  int textPixelWidth = 0;
  int offset = 0;
  pms_data latest = {0,0,0,0,0};

  // initialize display
  matrix.begin();
  matrix.setTextWrap(false);
  matrix.setBrightness(40);   // adjust brightness for tests
  matrix.setFont();           // default font (6x8)
  matrix.setTextColor(WHITE);

  // Od poslednite poraki primeni od TS - printaj na displej
  auto buildMessage = [&](const pms_data &s) {
    String m = "AirQ ";
    m += "T:" + String(s.temp) + "C ";
    m += "H:" + String(s.humid) + "% ";
    m += "P2:" + String(s.pm2_5) + " ";
    m += "P10:" + String(s.pm10) + " ";
    return m;
  };

  auto pixelWidth = [&](const String &s) -> int {
    // default Adafruit 5x7 font uses 6 px per char (5 px + 1 spacing)
    return s.length() * 6;
  };

  matrix.fillScreen(0);
  matrix.setCursor(0,0);
  matrix.print("KHIE @ FEIT");
  matrix.show();
  vTaskDelay(pdMS_TO_TICKS(800));

  // za da nemame prazna poraka na displejot - dokolku ima informacija - napravi nova poraka, dokolku nema nova informacija od queue - prati default poraka
  if (xQueueReceive(tsQueue, &latest, pdMS_TO_TICKS(1000)) == pdTRUE) {
    bigText = buildMessage(latest);
    textPixelWidth = pixelWidth(bigText);
  } else {
    latest = {0,0,0,0,0};
    bigText = buildMessage(latest);
    textPixelWidth = pixelWidth(bigText);
  }

  // offset starts at screen width so text enters from right
  offset = -MATRIX_W;

  for (;;) {
    // if there is a new queue snap available immediately, get it
    pms_data newdata;
    if (xQueueReceive(tsQueue, &newdata, 0) == pdTRUE) {
      // update latest, rebuild message
      if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        latest = newdata;
        xSemaphoreGive(dataMutex);
      }
      bigText = buildMessage(latest);
      textPixelWidth = pixelWidth(bigText);
      // resetiraj offset - da se pokaze nov aporaka od ddesno
      offset = -MATRIX_W;
    } else {
      // da se probudi i so notifikacija
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(SCROLL_DELAY_MS));
      // Dokolku ima dobieno notifikacija - probaj da go ispraznis queue
      if (uxQueueMessagesWaiting(tsQueue) > 0) {
        while (xQueueReceive(tsQueue, &newdata, 0) == pdTRUE) {
          if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            latest = newdata;
            xSemaphoreGive(dataMutex);
          }
          bigText = buildMessage(latest);
          textPixelWidth = pixelWidth(bigText);
          offset = -MATRIX_W;
        }
      }
    }

    // --- render the current window of bigText at horizontal position (MATRIX_W - offset) ---
    matrix.fillScreen(0);

    // draw text at (x,y) where x = MATRIX_W - offset (so offset increments move left)
    int x = MATRIX_W - offset;
    matrix.setCursor(x, 0);

    if (latest.temp < 10) matrix.setTextColor(BLUE);
    else if (latest.temp < 20) matrix.setTextColor(YELLOW);
    else if (latest.temp < 30) matrix.setTextColor(ORANGE);
    else matrix.setTextColor(RED);
    if (latest.humid < 30) matrix.setTextColor(LIME);
    else if (latest.humid < 60) matrix.setTextColor(YELLOW);
    else matrix.setTextColor(ORANGE);

    if (latest.pm2_5 < 35) matrix.setTextColor(GREEN);
    else if (latest.pm2_5 < 53) matrix.setTextColor(YELLOW);
    else if (latest.pm2_5 < 70) matrix.setTextColor(ORANGE);
    else matrix.setTextColor(RED);

    if (latest.pm10 < 50) matrix.setTextColor(GREEN);
    else if (latest.pm10 < 75) matrix.setTextColor(YELLOW);
    else if (latest.pm10 < 100) matrix.setTextColor(ORANGE);
    else matrix.setTextColor(RED);


    matrix.print(bigText);
    matrix.show();

    // increment offset (scroll step)
    offset++;
    //Koga offsetot e pogolem od textPixelWifth i sirinata na matricata - vrati se desno (loop)
    if (offset > textPixelWidth + MATRIX_W) {
      offset = -MATRIX_W; 
    }

  }
}

// Za da ima nesto prikazano na displejot - pocni od generalno - se 0 da prikazuva
void postInitialQueue() {
  pms_data init = {0,0,0,0, millis()};
  xQueueSend(tsQueue, &init, 0);
}

// === setup ===
void setup() {
  Serial.begin(115200);
  delay(100);

  // create RTOS objects
  dataMutex = xSemaphoreCreateMutex();
  tsQueue = xQueueCreate(4, sizeof(pms_data)); // small queue (4 entries)

  if (!dataMutex || !tsQueue) {
    Serial.println("Failed to create mutex/queue");
    for (;;);
  }

  WiFi.mode(WIFI_STA);
  ThingSpeak.begin(client);

  postInitialQueue();

  // create tasks
  xTaskCreatePinnedToCore(WiFiTask, "WiFiTask", 4096, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(ThingSpeakTask, "ThingSpeak", 8192, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(DisplayTask, "Display", 8192, NULL, 2, &displayTaskHandle, 1);

  Serial.println("Setup done, tasks started");
}

void loop() {
  vTaskDelay(portMAX_DELAY);
}
