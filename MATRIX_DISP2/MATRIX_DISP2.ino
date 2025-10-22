
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


SemaphoreHandle_t dataMutex = NULL; // nudi zastita na podatocite da ne bidat obraboteni nesakano od drug task
QueueHandle_t tsQueue = NULL;   // za prakjanje podatoci zemeni od TS kon taskot za prikazuvanje na displej
TaskHandle_t displayTaskHandle = NULL; // ke ni posluzi za notifikacii 

typedef struct {
  int temp;
  int humid;
  int pm2_5;
  int pm10;
} pms_data;

static const TickType_t SCROLL_DELAY_MS = 60; // zavisi od sirinata na displejot sto prikazuvame - za pomali displei podobro e pogolem delay - za poubavo da se vidi porakata
static const TickType_t TS_READ = 30000; // interval za citanje od ThingSpeak = 30s


  uint16_t color_temp(int t) { // funkcii za promena na bojata vo soodvetnost na vrednostite na T,H,PM2.5,PM10
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

    long status = ThingSpeak.getLastReadStatus();
    if (status == 200) {
      Serial.printf("TS OK: T=%d H=%d PM2.5=%d PM10=%d\n", 
        snap.temp, snap.humid, snap.pm2_5, snap.pm10);
      if (xQueueSend(tsQueue, &snap, 0) == pdTRUE) { // ako e == pdTRUE - znaci deka uspesno se stavile podatocite vo Queue - dokolku ne e znaci deka Queue e full ili nekoj drug error
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

  pms_data latest = {0,0,0,0};
  pms_data new_data;
  bool nw = false; // flag za dali pristignala novi podatoci - true koga ke pristignat - stavame flag - ne pravime odma update za da ne se presece prethodnata poraka
  bool fd = false; // pomosen flag - koga pocnuva displejot posle konektiranje (Connecting.. ke se trgne) i ke pocne da se pecati normalnata poraka za podatoci - no na pocetok 
  // se slucuva da nema zemeno podatoic od ThingSpeaj pa prvata poraka ke bidat site parametri (T,H,PM2.5, PM10) prikazani kako 0 - so ova ke go izbegneme toa

  // initialize display
  matrix.begin();
  matrix.setTextWrap(false);
  matrix.setBrightness(20);   
  matrix.setFont();           // default font (6x8) - edna bukva e siroka 6 pikseli 
  int offset = MATRIX_W; // pocnuvame od posledniot piksel (16,0) - vrti od desno kon levo

  for (;;) {
    matrix.fillScreen(0);
    if(WiFi.status() != WL_CONNECTED) { // koga ne e konektiran na WiFi prikazi Connecting..
      matrix.setTextColor(WHITE);
      matrix.setCursor(offset,0);
      matrix.print("Connecting.. ");
      matrix.show();
      offset--; 
      if (offset < -100) offset = MATRIX_W; // Connecting = 13 karakteri * 6 = 78 potreben offset - no staven pogolem
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    if (!fd) { // dokolku sme na pocetok - i nemame zemeno data uste od TS - proveri - ako nema printaj prazno (ova ke trae kratko - no koga ke zeme za brzo podatoci od TS - se printa
    // default poraka
      if (xQueueReceive(tsQueue, &new_data, 0) == pdTRUE) { // dokolku ima nesto stasano na Queue (od TS) - stavi fd = true - odi da printas normalna poraka za podatoci od TS
        fd = true;
        latest = new_data;
      } else { // printaj prazni mesta (ne prikazuj nisto) - koga ke dojde prvata informacija od TS (sto treba da bide za brzo) - ke prodolzi so normalna operacija
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
      // Normal data update 
      if (xQueueReceive(tsQueue, &new_data, 0) == pdTRUE) { // koga fd ke e true - prodolzi so normalna poraka - no ne printaj odma - samo setiraj flag
      // SO KORISTENJE NA NOTIFIKACIJA (TaskNotify) DISPLEJOT MOZE NAJBRZO VO ISTIOT MOMENT DA BIDE UPDATIRAN KOGA KE PRISTIGNE NOVA INFORMACIJA OD TS - no ne prikazuj naednas
      // za da ne se presece naglo prethodnata poraka sto bila isprintana - koga taa ke zavrsi odma ke pocne da se printa poraka so novi informaciii
        nw = true;
      }
    } 

    String msg = "KHIE@FEIT Vozduh ";
    msg += "T:" + String(latest.temp) + "C ";
    msg += "H:" + String(latest.humid) + "% ";
    msg += "PM2.5:" + String(latest.pm2_5) + " ";
    msg += "PM10:" + String(latest.pm10) + "    "; // presmetaj sirina na porakata + 4 prazni mesta za da ne se presece PM10 na kraj
    int msg_w  = msg.length() *6;

    matrix.setCursor(offset,0);

    matrix.setTextColor(WHITE);
    matrix.print("KHIE @ FEIT Vozduh: "); // vo belo startna poraka 
    
    matrix.setTextColor(color_temp(latest.temp)); //menuvaj boja vo soodvetnost so vrednosta na parametrite i printaj na displej
    matrix.print("T:");
    matrix.print(latest.temp);
    matrix.print("C "); // so matrix.print i pred matrix.show se do sega se smesutva samo vo buffer - koga ke se povika matrix.show ke bide isprintano na displej

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
    
    vTaskDelay(pdMS_TO_TICKS(SCROLL_DELAY_MS)); // za kontrola na brzinata na porakata
    if (offset < -msg_w) { // koga celosnata poraka ke ja snema od ekran - vrati se da printas nova poraka
      offset = MATRIX_W;
      if (nw) { // koga ke zavrsi prethodnata poraka - i sme imale nov podatok od notifikacijata - new  e setiran na true - zemi data of semafor
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

  // kreiraj mutex i queue
  dataMutex = xSemaphoreCreateMutex();
  tsQueue = xQueueCreate(4, sizeof(pms_data)); 

  if (!dataMutex || !tsQueue) {
    Serial.println("Failed to create mutex/queue");
    for (;;);
  }

  //kreiraj gi taskovie za displej i TS

  xTaskCreatePinnedToCore(ThingSpeakTask, "ThingSpeak", 8192, NULL, 1, NULL, 1); 
  xTaskCreatePinnedToCore(DisplayTask, "Display", 8192, NULL, 1, &displayTaskHandle, 1);

  Serial.println("Setup done");
}

void loop() { // vo loop portMAX_DELAY - za loop voopsto da ne odzima vreme pri izvrsuvanje na kodot - se se izvrsuva vo taskovi
  vTaskDelay(portMAX_DELAY);
}
