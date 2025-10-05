#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include <HardwareSerial.h>
#include "PMS5003T.h"
#include <ThingSpeak.h>
#include <WiFi.h>
#include <ArduinoOTA.h>

#define QUEUE_SIZE 5
#define PMS_SET 4
#define PMS_RX 16 //PMS TX - ESP32 RX
#define PMS_TX 17 // PMS RX - ESP32 TX
#define OLED_ADDRESS 0x3C
WiFiClient client;
const char* ssid = "StuHo-WiFi";
const char* password = "Only4StuHos";
unsigned long channelID = 3095045;
const char* APIkey = "WKQQSKL49OZQKBB6";

Adafruit_SSD1306 display(128,64,&Wire, -1); 
HardwareSerial pmsSerial(2);

QueueHandle_t pmsQ = NULL;
TimerHandle_t Timer;

typedef struct {
  int pm2_5 = 0;
  int pm10 = 0;
  int temp = 0;
  int humid = 0;
} pms_data;


void pmsTask(void *parameter) {
  for(;;) {
    pms_data data;
    uint8_t pms_buffer[31] = {0};

    if (pmsSerial.available()) {
      if (pmsSerial.find(0x42)) {
        pmsSerial.readBytes(pms_buffer,31);
        if (checkValue(pms_buffer,31)) {
          data.pm2_5 = transmitPM2_5(pms_buffer);
          data.pm10 = transmitPM10(pms_buffer);
          data.temp = transmitTemp(pms_buffer);
          data.humid = transmitHumid(pms_buffer);

          xQueueSend(pmsQ, &data, portMAX_DELAY);

          Serial.printf("PM2.5: %d  PM10: %d  Temp: %d C  Humid: %d%%\n",
                        data.pm2_5, data.pm10, data.temp, data.humid);
          vTaskDelay(1000 / portTICK_PERIOD_MS);
        } else {
          Serial.println("Error - Invalid Data!");
          vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
      }
    }
  }
}

void DisplayTask(void *parameter) {
  if(!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("OLED init failed!");
    while(1);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  for(;;) {
    pms_data data;
    if (xQueueReceive(pmsQ, &data, portMAX_DELAY)) {
      display.clearDisplay();
      display.setCursor(0,8);
      display.print("PM 2.5:  ");
      display.print(data.pm2_5,1);
      display.print(" ");
      display.cp437(true);

      display.setCursor(0,20);
      display.print("PM 10:  ");
      display.print(data.pm10,1);
      display.print(" ");

      display.setCursor(0,30);
      display.print("Temperature:  ");
      display.print(data.temp,1);
      display.println(" C");

      display.setCursor(0,40);
      display.print("Humidity:  ");
      display.print(data.humid,1);
      display.println(" %");
      
      display.display();
    }
  }
}

void sendThingSpeak(pms_data data) {
  ThingSpeak.setField(1, data.pm2_5);
  ThingSpeak.setField(2, data.pm10);
  ThingSpeak.setField(3, data.temp);
  ThingSpeak.setField(4, data.humid);
  int status = ThingSpeak.writeFields(channelID, APIkey);

  if (status == 200) {
    Serial.println("ThingSpeak update successful.");

  } else {
    Serial.println("ThingSpeak update failed, HTTP error: " + String(status));
  }
}

void TimerTS(TimerHandle_t Timer) {
  pms_data data;
  if (xQueueReceive(pmsQ, &data, portMAX_DELAY)) {
    sendThingSpeak(data);
  } else {
    Serial.println("No new data for ThingSpeak");
  }
}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  
  pmsSerial.begin(9600,SERIAL_8N1,PMS_RX,PMS_TX);
  pinMode(PMS_SET, OUTPUT);
  digitalWrite(PMS_SET, HIGH);
  Wire.begin();
  pmsQ = xQueueCreate(QUEUE_SIZE, sizeof(pms_data));

  if(pmsQ == NULL) {
    Serial.println("Failed to create queue");
    while (1);
  }
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(ssid,password);
  Serial.print("Connecting to WiFi ..");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" connected!");

  ThingSpeak.begin(client);

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
    4000,
    NULL,
    1,
    NULL,
    1
  );

  Timer = xTimerCreate("Timer",
                      pdMS_TO_TICKS(20000), // 20 s
                      pdTRUE,
                      (void*)0,
                      TimerTS);
  if (Timer != NULL) {
    xTimerStart(Timer,0);
  }

}

void loop() {
  // put your main code here, to run repeatedly:

}
