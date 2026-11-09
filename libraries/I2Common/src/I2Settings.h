#pragma once
// ============================================================================
//  I2Settings - persistent settings in ESP32 NVS (flash) with power-loss and
//  corruption protection.
//
//  Why NVS instead of EEPROM emulation:
//   * NVS is wear-levelled and every entry carries its own CRC; an entry is
//     written as a NEW entry and the old one is invalidated afterwards, so a
//     reset / power loss during a write leaves either the old or the new value.
//   * On top of that the whole settings block is stored TWICE (slot A / slot B)
//     with a magic number, version, size and CRC32. At boot the first valid
//     slot wins; a damaged slot is repaired from the good one.
//   * Every write is read back and verified.
//   * Nothing is written when the values did not change (flash wear).
//   * Optional hook is called before each flash write so the external 555
//     watchdog can be kicked and cannot fire in the middle of a write.
// ============================================================================
#include <Arduino.h>

#define I2_SETTINGS_VERSION 1
#define I2_CHID_MIN_DIGITS  6      // ThingSpeak channel IDs: 6 digits (old) ...
#define I2_CHID_MAX_DIGITS  8      // ... 7 digits today (3.2 M channels), 8 = headroom
#define I2_API_KEY_LEN      16

enum I2Role : uint8_t { I2_ROLE_SENSOR = 1, I2_ROLE_MATRIX = 2 };

struct I2Calib {            // corrected = gain * raw + offset
  float gain;
  float offset;
};

struct I2Settings {
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  char     ssid[33];
  char     wifiPass[65];
  uint32_t channelId;       // ThingSpeak data channel
  char     writeKey[17];    // sensor unit
  char     readKey[17];     // matrix unit ("" = public channel)
  uint32_t cfgChannelId;    // ThingSpeak channel holding calibration (0 = none)
  char     cfgReadKey[17];
  char     accessCode[17];  // serial console "general password"
  uint8_t  fastMode;        // 0 = normal, 1 = fast
  uint8_t  calAuto;         // 1 = apply remote calibration without asking
  uint16_t sleepSec;        // sleep time in normal mode
  uint32_t calVersion;      // version of the calibration set in use
  I2Calib  pm25, pm10, temp, humid;
  float    slmOffsetDb;     // sound level calibration offset
  uint32_t crc;             // CRC32 of everything above (must stay last)
};

// Factory values handed over by the sketch (library code cannot see sketch macros).
struct I2Defaults {
  I2Role      role;
  const char* fwVersion;
  const char* hostname;
  const char* ssid;
  const char* wifiPass;
  uint32_t    channelId;
  const char* writeKey;
  const char* readKey;
  const char* accessCode;
  const char* svcSsid;      // service network (phone hotspot) - may be ""
  const char* svcPass;
  const char* otaPass;
  const char* apSsid;       // config-portal access point (opens only when needed)
  const char* apPass;       // WPA2 password of that access point (8+ chars)
  uint16_t    sleepSec;
  I2Calib     pm25, pm10, temp, humid;
};

// --- lifecycle --------------------------------------------------------------
void  i2cfgBegin(const I2Defaults& d);
const I2Defaults& i2cfgDefaults();
bool  i2cfgLoadedFromNvs();
void  i2cfgSetPreWriteHook(void (*fn)());   // e.g. hardware watchdog kick

// --- access (thread safe) ---------------------------------------------------
void     i2cfgSnapshot(I2Settings& out);
bool     i2cfgCommit(const I2Settings& next);      // validate-free: callers validate
bool     i2cfgFactoryReset();
bool     i2cfgCheckAccessCode(const char* code);   // constant-time compare
uint8_t  i2cfgFastMode();
uint16_t i2cfgSleepSec();
float    i2cfgSlmOffsetDb();
void     i2cfgGetCal(I2Calib out[4]);              // pm25, pm10, temp, humid
bool     i2cfgApplyCal(uint32_t version, const I2Calib cal[4]);

// --- validation helpers -----------------------------------------------------
bool i2ValidChannelId(const String& s, uint32_t* out);   // 6..8 digits, no leading 0
bool i2ValidApiKey(const String& s);                     // exactly 16 [A-Za-z0-9]
bool i2CalSane(const I2Calib& c);                        // gain 0.1..10, |offset| <= 500
// "version,g25,o25,g10,o10,gT,oT,gH,oH"
bool i2CalParseCsv(const char* csv, uint32_t* version, I2Calib out[4]);
inline float i2CalApply(const I2Calib& c, float raw) { return c.gain * raw + c.offset; }

// safe string copy
inline void i2Copy(char* dst, size_t n, const char* src) {
  if (!n) return;
  strncpy(dst, src ? src : "", n - 1);
  dst[n - 1] = 0;
}
