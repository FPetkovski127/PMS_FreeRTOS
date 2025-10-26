#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <HardwareSerial.h>
#include "PMS5003T.h"
#include <ThingSpeak.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <EEPROM.h>
#include <WiFiManager.h>

#define QUEUE_SIZE 15
#define PMS_SET 4
#define PMS_RX 16 //PMS TX - ESP32 RX
#define PMS_TX 17 // PMS RX - ESP32 TX
#define LED_PIN 2
#define OLED_ADDRESS 0x3C
#define EEPROM_SIZE 128 
#define EEPROM_KEY_OFFSET 0
#define CHANNEL_NUM_OFFSET 4
#define CHANNEL_WRITE_OFFSET 27
#define CHANNEL_MODE_OFFSET 42

// vreminja za kontrola na rezimi za rabota - fast i normal 
#define FAST_W 60000UL // 1 minuta zimaj podatoci vo fast mode pred avg i TS
#define NORMAL_POWERON 30000UL // 30 s warmup
#define NORMAL_GETDATA 30000UL // 30 s za citanje podatoci
#define NORMAL_IDLE 120000UL // 120 s idle 

const char EEPROM_KEY[4] = {'F','E','I','T'}; 

enum PMS_STATES { POWERON, GETDATA, POWEROFF, IDLE};
volatile PMS_STATES fsm_state = POWERON;

WiFiClient client;
WiFiManager wm;
WiFiManagerParameter channel_num_param; // channelID или channel number od TS
WiFiManagerParameter channel_write_param; // Write key for TS
WiFiManagerParameter channel_mode_param; // normal or fast
//const char* ssid = "StuHo-WiFi"; 
//const char* password = "Only4StuHos";
String channelID = "3095045"; 
String writeKey = "WKQQSKL49OZQKBB6";

const char* OTA_PW = "FEIT2025";
const char* OTA_UN = "PMS_OTA_Device"; //username za ESP32 konekcija so OTA 

int fast_mode = 0; // 0 = normal, 1 = fast
bool pms_active = false;
int wait_cycles = 0, idle_cycles = 0;
Adafruit_SSD1306 display(128,64,&Wire, -1);  // Wire interface -> SDA = pin 21 , SCL = pin 22 (default I2C pins)
HardwareSerial pmsSerial(2);

QueueHandle_t pmsQ = NULL;
QueueHandle_t otaQ = NULL; // for ota progress
SemaphoreHandle_t mutex_data = NULL;

typedef struct {
  int pm2_5 = 0;
  int pm10 = 0;
  int temp = 0;
  int humid = 0;
} pms_data;
pms_data latestData = {0,0,0,0};
volatile bool params_set = false;

void EEPROMinit() {
  EEPROM.begin(EEPROM_SIZE);
}
bool eepromKey() {
  char buf[4];
  for (int i = 0; i < 4; i++)
  EEPROM.get(EEPROM_KEY_OFFSET + i, buf[i]);
  return (memcmp(buf,EEPROM_KEY, 4) == 0); // proveri dali go ima istiot kluc
}
void loadEEPROM() {
  EEPROMinit();
  if (!eepromKey()) {
    Serial.println("EEPROM key missing - using defaults");
    channelID = "3095045";
    writeKey = "WKQQSKL49OZQKBB6";
    fast_mode = 0;
  } else {
    char cb[8]; memset(cb,0,sizeof(cb)); // da se osigurame deka vo cb buferot nema nekoi random bajti - site 0
    for (int i = 0; i<7; i++) {
      char c;
      EEPROM.get(CHANNEL_NUM_OFFSET + i, c);
      cb[i] = c;
    }
    cb[7] = '\0';
    channelID = String(cb);
    char kb[17]; memset(kb,0,sizeof(kb));
    for (int i = 0; i<16; i++) {
      char c;
      EEPROM.get(CHANNEL_WRITE_OFFSET + i, c);
      kb[i] = c;
    }
    kb[16] = '\0';
    writeKey = String(kb);

    int mode = 0; 
    EEPROM.get(CHANNEL_MODE_OFFSET, mode);
    fast_mode = mode;
  }
  Serial.println("-- Loading from EEPROM --");
  Serial.println("Channel: " + channelID);
  Serial.println("Key: " + writeKey);
  Serial.println(String("Mode: ") + (fast_mode ? "FAST" : "NORMAL"));
}

void saveEEPROM() {
  for (int i = 0; i<4; i++) EEPROM.put(EEPROM_KEY_OFFSET + i, EEPROM_KEY[i]);
  char cb[8]; memset(cb,0,sizeof(cb));
  channelID.toCharArray(cb,sizeof(cb));
  for (int i = 0; i < 7; i++) EEPROM.put(CHANNEL_NUM_OFFSET + i, cb[i]);
  char kb[17]; memset(kb,0,sizeof(kb));
  writeKey.toCharArray(kb,sizeof(kb));
  for (int i = 0; i < 16; i++) EEPROM.put(CHANNEL_WRITE_OFFSET + i, kb[i]);
  EEPROM.put(CHANNEL_MODE_OFFSET, fast_mode);
  EEPROM.commit();
  Serial.println("EEPROM saved");
}

String getParam(String name) {
  String value;
  if (wm.server->hasArg(name)) value = wm.server->arg(name);
  return value;
}

void saveParamCallback() {
  Serial.println("[WiFiManager] saveParamCallback fired");
  String ch = getParam("channelnum"); // od http zemi gi parametrite - smesti gi vo ch  i wkey
  String wkey = getParam("channelwriteapi");
  String mode = getParam("channelmode"); // http - T ili true ako checkbox za fast mode e cekirano

  Serial.println("channelnum: " + ch + " len=" + String(ch.length()));
  Serial.println("channelwriteapi: " + wkey + " len=" + String(wkey.length()));
  Serial.println("channelmode: " + mode);

  bool ok = true;
  if (ch.length() != 7) { Serial.println("channelnum bad length"); ok = false; }
  if (wkey.length() != 16) { Serial.println("write key bad length"); ok = false; }
  if (ok) {
    for (int i = 0; i<7; i++) 
      EEPROM.put(CHANNEL_NUM_OFFSET + i, ch.charAt(i));
    for (int i=0;i<16;i++) 
      EEPROM.put(CHANNEL_WRITE_OFFSET + i, wkey.charAt(i));
    if (mode.length() > 0 && mode.charAt(0) == 'T') 
      EEPROM.put(CHANNEL_MODE_OFFSET, 1);
    else 
      EEPROM.put(CHANNEL_MODE_OFFSET, 0);

    for (int i=0;i<4;i++) 
      EEPROM.put(EEPROM_KEY_OFFSET + i, EEPROM_KEY[i]);
      params_set = true;
      Serial.println("Parameters staged to EEPROM"); //  commit za vo memorija - vo setup

  } else {
    Serial.println("invalid parameters");
  }
}

void initOTA() {
  ArduinoOTA.setHostname(OTA_UN);
  ArduinoOTA.setPassword(OTA_PW);
  ArduinoOTA.onStart([](){
    Serial.println("OTA initialization - starting");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nOTA Finished");
    int finished = 100;
    if (otaQ) xQueueSend(otaQ, &finished, 5 / portTICK_PERIOD_MS);
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total){
    int percent = (int)((progress * 100) / total);
    if (otaQ) {
      xQueueSend(otaQ, &percent, 0);
    }
    Serial.printf("OTA Progress: %u%%\r", percent);
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");

    int err = -1;
    if (otaQ) xQueueSend(otaQ, &err, 0);
  });

  ArduinoOTA.begin();
  Serial.println("OTA Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void OTATask(void *pvParameters) {
  (void) pvParameters;
  for (;;) {
    ArduinoOTA.handle();
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}


void pmsTask(void *parameter) {
  uint8_t pms_buffer[31];
  unsigned long fast_w = 0;
  unsigned long normal_w = 0;

  pms_data accum = {0,0,0,0};
  int sample_cnt = 0;
  unsigned long state_start = 0;
  bool first = true; // za first entry

  for(;;) {
    if (fast_mode) {
      fsm_state = GETDATA;
      if (!pms_active) {
        digitalWrite(PMS_SET, HIGH);
        pms_active = true;
        Serial.println("Turning PMS on");
      }
      
      if (fast_w == 0) {
        fast_w = millis();
        accum = {0,0,0,0};
        sample_cnt = 0;
      }

      if(pmsSerial.find((char)0x42)) {
        size_t r = pmsSerial.readBytes(pms_buffer,31);
        if (r == 31 && checkValue(pms_buffer,31)) {
          pms_data d;
          d.pm2_5 = transmitPM2_5(pms_buffer);
          d.pm10  = transmitPM10(pms_buffer);
          d.temp  = transmitTemp(pms_buffer);
          d.humid = transmitHumid(pms_buffer);

          accum.pm2_5 += d.pm2_5;
          accum.pm10  += d.pm10;
          accum.temp  += d.temp;
          accum.humid += d.humid;
          sample_cnt++;
          if(xSemaphoreTake(mutex_data, pdMS_TO_TICKS(20)) == pdTRUE) {
            latestData = d;
            xSemaphoreGive(mutex_data);
          }
          xQueueSend(pmsQ,&d,0);
          Serial.printf("Sample %d: %d %d %dC %d%%\n", sample_cnt, d.pm2_5, d.pm10, d.temp, d.humid);
        }
      }
      if (millis() - fast_w >= FAST_W) {
        if (sample_cnt > 0) {
          pms_data avg;
          avg.pm2_5 = accum.pm2_5 / sample_cnt;
          avg.pm10  = accum.pm10  / sample_cnt;
          avg.temp  = accum.temp  / sample_cnt;
          avg.humid = accum.humid / sample_cnt;
          sendThingSpeak(avg);
          Serial.printf("Sent average of %d samples\n", sample_cnt);
        } else {
          Serial.println("No samples collected");
        }
        accum = {0,0,0,0};
        sample_cnt = 0;
        fast_w = millis();
      }
      vTaskDelay(pdMS_TO_TICKS(200)); // sekoi 200 ms probaj da citas od PMS
    } else {
      switch (fsm_state) {
        case POWERON:
          if (first) {
            Serial.println("POWERON");
            digitalWrite(PMS_SET, HIGH);
            pms_active = true;
            state_start = millis();
            // prepare accumulators for this cycle
            accum = {0,0,0,0};
            sample_cnt = 0;
            first = false;
          }
          if (millis() - state_start >= NORMAL_POWERON) {
            fsm_state = GETDATA;
            first = true; // 
          }
          break;

        case GETDATA:
         if (first) {
            Serial.println("GETDATA");
            normal_w = millis(); // start 30s sampling window
            first = false;
          }

          // attempt to read frames as they arrive
          if (pmsSerial.find((char)0x42)) {
            size_t r = pmsSerial.readBytes(pms_buffer, 31);
            if (r == 31 && checkValue(pms_buffer,31)) {
              pms_data d;
              d.pm2_5 = transmitPM2_5(pms_buffer);
              d.pm10  = transmitPM10(pms_buffer);
              d.temp  = transmitTemp(pms_buffer);
              d.humid = transmitHumid(pms_buffer);

              accum.pm2_5 += d.pm2_5;
              accum.pm10  += d.pm10;
              accum.temp  += d.temp;
              accum.humid += d.humid;
              sample_cnt++;

              // update latestData thread-safe
              if (xSemaphoreTake(mutex_data, pdMS_TO_TICKS(20)) == pdTRUE) {
                latestData = d;
                xSemaphoreGive(mutex_data);
              }

              xQueueSend(pmsQ, &d, 0);
              Serial.printf("Sample %d: %d %d %dC %d%%\n", sample_cnt, d.pm2_5, d.pm10, d.temp, d.humid);
            }
          }

          // after sampling window completes -> average & send
          if (millis() - normal_w >= NORMAL_GETDATA) {
            if (sample_cnt > 0) {
              pms_data avg;
              avg.pm2_5 = accum.pm2_5 / sample_cnt;
              avg.pm10  = accum.pm10  / sample_cnt;
              avg.temp  = accum.temp  / sample_cnt;
              avg.humid = accum.humid / sample_cnt;

              sendThingSpeak(avg);
              Serial.printf("Sent average of %d samples\n", sample_cnt);
            } else {
              Serial.println("No samples collected in GETDATA");
            }
            fsm_state = POWEROFF;
            first = true;
          }
          break;

        case POWEROFF:
          if (first) {
            Serial.println("POWEROFF - turning sensor off");
            digitalWrite(PMS_SET, LOW);
            pms_active = false;
            state_start = millis();
            first = false;
          }
          // 3 s vo POWEROFF pred IDLE
          if (millis() - state_start >= 3000UL) {
            fsm_state = IDLE;
            first = true;
          }
          break;

        case IDLE:
          if (first) {
            Serial.println("IDLE (sleeping)");
            state_start = millis();
            first = false;
          }
          if (millis() - state_start >= NORMAL_IDLE) {
            fsm_state = POWERON;
            first = true;
          }
          break;
      }
      vTaskDelay(pdMS_TO_TICKS(150));
    }
  }

}

void LEDTask(void *parameter) { // pin 2 -  built in dioda na esp32 kontrolirame intenzitet na trepkanje so mode
  for (;;) {
    int delay_time;
    if (fast_mode) 
      delay_time = 100; //fast blink
    else
      delay_time = 500; //normal blink
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    vTaskDelay(pdMS_TO_TICKS(delay_time));
  } 
}

void OTAProgress(int pct) { // otcrtuvame progress bar spored informacii od pct = OTA OnProgress
  const int X = 8;
  const int Y = 60;
  const int W = 112;
  const int H = 6;

  display.fillRect(0,Y-14,128,18,SSD1306_BLACK); //prazen del
  display.drawRect(X-1,Y-1,W+2,H+2,SSD1306_WHITE); //border
  int filled = (W * pct) / 100;
  if (filled > 0) {
    display.fillRect(X,Y,filled,H,SSD1306_WHITE);
  }
    display.setCursor(X,Y-10);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.print("OTA");
  display.print(pct);
  display.print("%");
  display.display();
}

void DisplayTask(void *parameter) {
  if(!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("OLED init failed!");
    for(;;) vTaskDelay(portMAX_DELAY);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  for(;;) {
    // zemi podatoci od queue - zapisi vo latest data ako sempaforot e sloboden
    pms_data newd;
    if (xQueueReceive(pmsQ, &newd, pdMS_TO_TICKS(50)) == pdTRUE) {
      if (xSemaphoreTake(mutex_data, pdMS_TO_TICKS(20)) == pdTRUE) {
        latestData = newd;
        xSemaphoreGive(mutex_data);
      }
    }

    // prikazi sostojbi na FSM celo vreme na displej bez da se blokira dodeka da zeme podatok od Queue - vaka celo vreme ke se menuvaat sostojbite na 
    //displej kako sto se vo tek - koga ima procitano i mutex a sloboden - prati na snap da se prikaze na displej, ako ne na displej ke bide prikazano
    //poslednoto otcituvanje a sostojbite ke se prikazuvaat kako se menuvaat za pred novoto iscituvanje 
    // Ovaka nemame nikakov prekin i moze na displej da se sledi sekoja FSM sostojba
    pms_data snap;
    if (xSemaphoreTake(mutex_data, pdMS_TO_TICKS(20)) == pdTRUE) {
      snap = latestData;
      xSemaphoreGive(mutex_data);
    }
    display.clearDisplay();
    display.setCursor(0,0);
    display.print("FSM: ");
    switch (fsm_state) {
      case POWERON: display.print("POWERON"); break;
      case GETDATA: display.print("GETDATA"); break;
      case POWEROFF: display.print("POWEROFF"); break;
      case IDLE: display.print("IDLE"); break;
    }
    display.setCursor(0,16);
    display.printf("PM2.5: %d  PM10: %d", snap.pm2_5, snap.pm10);
    display.setCursor(0,28);
    display.printf("Temp: %dC  Hum: %d%%", snap.temp, snap.humid);
    display.setCursor(0,48);
    display.print(fast_mode ? "Mode: FAST" : "Mode: NORMAL"); // prikazi vo koj rezim na rabota sme
    display.display();

    int pct;
    if (xQueueReceive(otaQ, &pct, 0) == pdTRUE) { // otpecati go progress bar na displej, dokolku progress = -1 -> error ispecati
      if (pct >= 0 && pct <= 100) OTAProgress(pct);
      else if (pct == -1) {
        display.fillRect(0,50,128,14,SSD1306_BLACK);
        display.setCursor(8,50);
        display.print("OTA Error");
        display.display();
      }
    }

    vTaskDelay(pdMS_TO_TICKS(200)); 
  }
}

void sendThingSpeak(pms_data data) { // zapisi podatoci vo TS
  ThingSpeak.setField(1, data.pm2_5);
  ThingSpeak.setField(2, data.pm10);
  ThingSpeak.setField(3, data.temp);
  ThingSpeak.setField(4, data.humid);
  long status = ThingSpeak.writeFields(channelID.toInt(), writeKey.c_str());

  if (status == 200) {
    Serial.println("ThingSpeak update successful.");
  } else {
    Serial.println("ThingSpeak update failed, HTTP error: " + String(status));
  }
}


void SerialCommandTask(void *pv) {
  (void) pv;
  String input = "";
  for (;;) {
    while (Serial.available()) {
      char c = Serial.read();
      if (c == '\n' || c == '\r') {
        if (input.length() > 0) {
          input.trim();
          if (input.equalsIgnoreCase("fast")) {
            fast_mode = 1; 
            saveEEPROM();
            Serial.println("FAST mode");
          } else if (input.equalsIgnoreCase("normal")) {
            fast_mode = 0; 
            saveEEPROM();
            Serial.println("NORMAL mode");
          } else if (input.equalsIgnoreCase("show")) {
            Serial.println("Channel: " + channelID);
            Serial.println("Write Key: " + writeKey);
            Serial.println("Mode: " + String((fast_mode) ? "FAST":"NORMAL"));
          } else if (input.startsWith("setkey ")) { // dodadena funkcija vo task za Serial Monitor input - da mozam lesno da promenam WriteKey vo EEPROM dokolku ima potreba
          // najdov greska predtoa - kade sto vo baferot ne se zacuvuval NULL terminator - pa ne bese zacuvan celosno Write Key vo EEPROM - pa ova za polesno da smenam - mesto da 
          // cepkam WiFi settings i AP konekcija - no sega i toa e popraveno i WriteKey uste na pocetok ke bide zacuvan kako sto treba vo EEPROM
              String newKey = input.substring(7);
              if (newKey.length() == 16) {
                writeKey = newKey;
                saveEEPROM();
                Serial.println("Write Key saved");
              }  else {
                Serial.println("Error: WriteKey isn't logn enough");
              } 
          } else {
            Serial.println("Commands: fast | normal | show | setKey");
          }
          input = "";
        }
      } else {
        input += c;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  delay(2000);
  pinMode(PMS_SET, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(PMS_SET, LOW);
  
  pmsSerial.begin(9600,SERIAL_8N1,PMS_RX,PMS_TX);
  Wire.begin();

  pmsQ = xQueueCreate(QUEUE_SIZE, sizeof(pms_data));
  otaQ = xQueueCreate(QUEUE_SIZE-5, sizeof(int));
  mutex_data = xSemaphoreCreateMutex();

  if(!pmsQ || !otaQ || !mutex_data) {
    Serial.println("Failed to create queue/mmutex");
    for(;;) vTaskDelay(portMAX_DELAY);
  }
    
  const char* channel_num_html = "<br/><label for='channelnum'>ThingSpeak Channel Number</label><input type='text' name='channelnum' maxlength='7'>";
  new (&channel_num_param) WiFiManagerParameter(channel_num_html);
  wm.addParameter(&channel_num_param);

  const char* channel_write_html = "<br/><label for='channelwriteapi'>ThingSpeak Channel Write API Key</label><input type='text' name='channelwriteapi' maxlength='16'>";
  new (&channel_write_param) WiFiManagerParameter(channel_write_html);
  wm.addParameter(&channel_write_param);

  new (&channel_mode_param) WiFiManagerParameter("channelmode", "Fast Mode", "T", 1, "type=\"checkbox\"");
  wm.addParameter(&channel_mode_param);

  wm.setSaveParamsCallback(saveParamCallback);

  // Probaj autoconnect = ke otvori AP ako e neuspesno
  Serial.println("Starting WiFiManager autoConnect (AP: AqsAP-WiFi if needed) ...");
  bool res = wm.autoConnect("AqsAP-WiFi"); // ke blokira se dodeka ne mu dademe Wifi mreza parametri preku prompt od AP
  if (!res) {
    Serial.println("WiFiManager autoConnect failed - restarting...");
    delay(2000);
    ESP.restart();
  }
  Serial.println("WiFi connected");

  // Dokolku se popolneti informaciite za writeKey, channelID - commit - zacuvaj - promeni vo EEPROM
  if (params_set) {
    EEPROM.commit(); 
    params_set = false;
    Serial.println("Saved parameters from portal to EEPROM");
  }

  loadEEPROM();

  ThingSpeak.begin(client);
  initOTA();

  xTaskCreatePinnedToCore(
    SerialCommandTask, 
    "SerialCmd", 
    3000, 
    NULL, 
    1, 
    NULL, 
    1
  );
  
  xTaskCreatePinnedToCore(
    LEDTask,
    "LED Task",
    2000,
    NULL,
    1,
    NULL,
    1
  );
  xTaskCreatePinnedToCore(
    OTATask,
    "otaTask",
    8192,
    NULL,
    1, 
    NULL,
    1
  );

  xTaskCreatePinnedToCore(
    pmsTask,
    "pmsTask",
    8000,
    NULL,
    1,
    NULL,
    1
  );

  xTaskCreatePinnedToCore(
    DisplayTask,
    "DisplayTask",
    6000,
    NULL,
    1,
    NULL,
    1
  );

  Serial.println("Setup done");
}

void loop() {
  // put your main code here, to run repeatedly:
  vTaskDelay(portMAX_DELAY); // da ne zima vreme od CPU - nisto ne se izvrsuva vo 
  // void loop - suspendiraj go kompletno (ako go ostavime prazno - celo vreme interno ke se 
  //povikuva - pomalce vreme za drugite taskovi)
}
