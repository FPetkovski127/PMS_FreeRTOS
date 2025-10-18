
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


SemaphoreHandle_t dataMutex = NULL;
QueueHandle_t tsQueue = NULL;          
TaskHandle_t displayTaskHandle = NULL; // ke ni posluzi za notifikacii 

typedef struct {
  int temp;
  int humid;
  int pm2_5;
  int pm10;
  unsigned long stamp;
} pms_data;

static const TickType_t SCROLL_DELAY_MS = 60; // zavisi od sirinata na displejot sto prikazuvame - za pomali displei podobro e pogolem delay - za poubavo da se vidi porakata
static const TickType_t TS_READ = 30000; // interval za citanje od ThingSpeak = 30sa


  uint16_t color_temp(int t) {
    if (t<10) return BLUE;
    else if (t < 20) return YELLOW;
    else if (t < 30) return ORANGE;
    else 
    return RED;
  };
  uint16_t color_humid(int h) {
    if (h<30) return LIME;
    else if (h < 60) return YELLOW;
    else 
    return ORANGE;
  };
  uint16_t color_PM2_5(int p) {
    if (p < 35) return GREEN;
    else if (p < 53) return YELLOW;
    else if (p < 70) return ORANGE;
    else
    return RED;
  };
  uint16_t color_PM10(int p) {
    if (p < 50) return GREEN;
    else if (p < 75) return YELLOW;
    else if (p < 100) return ORANGE;
    else
    return RED;
  };


// Cita od ThingSpeak - prakja podatoci vo queue - prakja notifikacija kon displejot dokolku ima nova informacija procitano od TS
void ThingSpeakTask(void *param) {
  (void) param;
  pms_data snap;
  WiFi.mode(WIFI_STA);
  Serial.printf("Connecting ..");
  WiFi.begin(ssid,pass);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 45000) {
    Serial.print(".");
    vTaskDelay(pdMS_TO_TICKS(500));
  }
  Serial.println(WiFi.status() == WL_CONNECTED ? "\nConnected" : "\nFailed to connect");
  ThingSpeak.begin(client);
  for(;;) {
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi lost, reconnecting...");
      WiFi.reconnect();
      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }
    snap.temp  = ThingSpeak.readIntField(ChannelID, 3, ReadKey);
    snap.humid = ThingSpeak.readIntField(ChannelID, 4, ReadKey);
    snap.pm2_5 = ThingSpeak.readIntField(ChannelID, 1, ReadKey);
    snap.pm10  = ThingSpeak.readIntField(ChannelID, 2, ReadKey);
    snap.stamp = millis();

    long status = ThingSpeak.getLastReadStatus();
    if (status == 200) {
      Serial.printf("TS OK: T=%d H=%d PM2.5=%d PM10=%d\n", 
        snap.temp, snap.humid, snap.pm2_5, snap.pm10);
      if (xQueueSend(tsQueue, &snap, 0) == pdTRUE) {
        xTaskNotifyGive(displayTaskHandle);
      }
    } else {
      Serial.printf("ThingSpeak error %ld\n", status);
    }

    vTaskDelay(pdMS_TO_TICKS(TS_READ));
  }
}


void DisplayTask(void *param) {
  (void) param;

  pms_data latest = {0,0,0,0,0};
  pms_data new_data;
  bool nw = false;
  bool fd = false;

  // initialize display
  matrix.begin();
  matrix.setTextWrap(false);
  matrix.setBrightness(20);   
  matrix.setFont();           // default font (6x8)
  int offset = MATRIX_W;

  for (;;) {
    matrix.fillScreen(0);
    if(WiFi.status() != WL_CONNECTED) {
      matrix.setTextColor(WHITE);
      matrix.setCursor(offset,0);
      matrix.print("Connecting.. ");
      matrix.show();
      offset--;
      if (offset < -120) offset = MATRIX_W;
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    if (!fd) {
      if (xQueueReceive(tsQueue, &new_data, 0) == pdTRUE) {
        fd = true;
        latest = new_data;
      } else {
        matrix.setTextColor(WHITE);
        matrix.setCursor(offset, 0);
        matrix.print("  ");
        matrix.show();
        offset--;
        if (offset < -20) offset = MATRIX_W;
        vTaskDelay(pdMS_TO_TICKS(100));
        continue;
      }
    } else {
      // Normal data update case
      if (xQueueReceive(tsQueue, &new_data, 0) == pdTRUE) {
        nw = true;
      }
    } 

    String msg = "KHIE@FEIT Vozduh ";
    msg += "T:" + String(latest.temp) + "C ";
    msg += "H:" + String(latest.humid) + "% ";
    msg += "PM2.5:" + String(latest.pm2_5) + " ";
    msg += "PM10:" + String(latest.pm10) + "    ";
    int msg_w  = msg.length() *6;

    matrix.setCursor(offset,0);

    matrix.setTextColor(WHITE);
    matrix.print("KHIE @ FEIT Vozduh: ");
    
    matrix.setTextColor(color_temp(latest.temp));
    matrix.print("T:");
    matrix.print(latest.temp);
    matrix.print("C ");

    matrix.setTextColor(color_humid(latest.humid));
    matrix.print("H:");
    matrix.print(latest.humid);
    matrix.print("% ");

    matrix.setTextColor(color_PM2_5(latest.pm2_5));
    matrix.print("PM2.5:");
    matrix.print(latest.pm2_5);
    matrix.print(" ");

    matrix.setTextColor(color_PM10(latest.pm10));
    matrix.print("PM10:");
    matrix.print(latest.pm10);
    matrix.print(" ");

    matrix.show();
    offset--;
    
    vTaskDelay(pdMS_TO_TICKS(SCROLL_DELAY_MS));
    if (offset < -msg_w) {
      offset = MATRIX_W;
      if (nw) {
        if(xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
          latest = new_data;
          xSemaphoreGive(dataMutex);
        }
        nw = false;
      }
    }
    
  }
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

  xTaskCreatePinnedToCore(ThingSpeakTask, "ThingSpeak", 8192, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(DisplayTask, "Display", 8192, NULL, 2, &displayTaskHandle, 1);

  Serial.println("Setup done, tasks started");
}

void loop() {
  vTaskDelay(portMAX_DELAY);
}
