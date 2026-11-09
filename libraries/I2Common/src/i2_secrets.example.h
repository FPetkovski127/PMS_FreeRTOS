#pragma once
// ============================================================================
//  FACTORY DEFAULTS / SECRETS  -  edit here, re-flash once, then everything
//  else is changed from the serial console (no re-flash needed).
//  Do NOT publish this file (add it to .gitignore).
// ============================================================================

// Network used when nothing was configured yet (and as the first choice later
// if the user did not set a different one with 'setup').
#define I2_DEFAULT_WIFI_SSID   "StuHo-WiFi"
#define I2_DEFAULT_WIFI_PASS   "Only4StuHos"

// SERVICE network (e.g. a phone hotspot). If the configured network cannot be
// reached the unit falls back to this one, so OTA firmware upload is always
// possible (before the config-portal access point is considered).
#define I2_SERVICE_WIFI_SSID   "FEIT-SERVICE"
#define I2_SERVICE_WIFI_PASS   "change-me-service"

// Config-portal access point (the original WiFiManager way). It is only opened
// when no network can be reached for a while, or on demand with the console
// command 'portal'. WPA2-protected, closes after 3 minutes, and nothing is
// applied unless the ACCESS CODE (below) is typed on the portal page.
#define I2_AP_SSID             "AqsAP-WiFi"
#define I2_AP_PASSWORD         "FEIT_KHIE_AP"

// Password required by ArduinoOTA (espota / Arduino IDE network port).
#define I2_OTA_PASSWORD        "FEIT2026"

// "Access code" - must be entered on the serial console (login <code>) before
// any parameter can be changed. Can be changed later with 'passwd'.
#define I2_DEFAULT_ACCESS_CODE "KHIE-2026"
