#pragma once
// ============================================================================
//  I2Wifi - connection supervisor + ArduinoOTA + WiFiManager config portal
//
//  Connection order
//   1. the CONFIGURED network (stored in flash)
//   2. the SERVICE network (phone hotspot, factory SSID/password in i2_secrets.h)
//      -> OTA firmware upload is always possible
//   3. the CONFIG PORTAL (WiFiManager access point) - the original way to give
//      the unit ANY WiFi network on the spot, using a phone:
//        * opens automatically only after the link was down > 60 s
//          (at most once per 15 min), or on demand with the console command 'portal'
//        * WPA2 protected, closes after 3 min
//        * the page has an ACCESS CODE field - without the right code nothing
//          is saved (wrong WiFi data typed by a stranger is ignored)
//  Recovery escalation when the link stays down:
//        new attempts -> WiFi stack off/on (2 min) -> restart (10 min)
// ============================================================================
#include <Arduino.h>

extern volatile int8_t g_i2OtaPercent;      // -1 idle, 0..100 progress, -2 error

void  i2wifiBegin();                        // creates the supervisor task
bool  i2wifiConnected();
bool  i2wifiOnServiceNet();
uint32_t i2wifiDownMs();                    // how long the link is down (0 = connected)
void  i2wifiRequestPortal();                // open the config portal now (console 'portal')
void  i2wifiSetOtaStartHook(void (*fn)());  // e.g. stop I2S / heavy tasks before flashing
void  i2wifiPrintStatus(Print& out);
