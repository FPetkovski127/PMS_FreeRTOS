#pragma once
// WiFi connection handling in one task (wifiTask):
//  1. configured network (from flash), 2. service network (phone hotspot), 3. config portal
//  (WiFiManager access point, needs the access code). If the link stays down the WiFi
//  stack is reset after 2 min and the chip restarts after 10 min. ArduinoOTA runs while connected.
#include <Arduino.h>

extern volatile int8_t g_i2OtaPercent;      // -1 idle, 0..100 progress, -2 error

void  i2wifiBegin();                        // creates the supervisor task
bool  i2wifiConnected();
bool  i2wifiOnServiceNet();
uint32_t i2wifiDownMs();                    // how long the link is down (0 = connected)
void  i2wifiRequestPortal();                // open the config portal now (console 'portal')
void  i2wifiSetOtaStartHook(void (*fn)());  // e.g. stop I2S / heavy tasks before flashing
void  i2wifiPrintStatus(Print& out);
