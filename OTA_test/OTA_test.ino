#include <ElegantOTA.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>

const char* ssid = "StuHo-WiFi";
const char* password = "Only4StuHos";

AsyncWebServer server(80);


void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid,password);
  Serial.println("");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.println(".");
  }
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200,"text/plain", "Hi! I am ESP32");
  }); 
  server.begin();
  Serial.println("HTTP server started");

  ElegantOTA.begin(&server);

}

void loop() {
 ElegantOTA.loop();
}