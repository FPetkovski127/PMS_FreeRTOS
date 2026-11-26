#include "I2Settings.h"
#include <Preferences.h>
#include <stddef.h>
#include <math.h>

static const char*    NVS_NS  = "i2cfg";
static const char*    KEY_A   = "cfgA";
static const char*    KEY_B   = "cfgB";
static const uint32_t MAGIC   = 0x46433249UL;   // "I2CF"

static Preferences       s_prefs;
static I2Settings        s_cfg;
static I2Defaults        s_def;
static SemaphoreHandle_t s_mtx = nullptr;
static bool              s_fromNvs = false;

static uint32_t crc32(const uint8_t* d, size_t n) {
  uint32_t c = 0xFFFFFFFFu;
  while (n--) {
    c ^= *d++;
    for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
  }
  return ~c;
}

static void seal(I2Settings& s) {
  s.magic = MAGIC;
  s.version = I2_SETTINGS_VERSION;
  s.size = sizeof(I2Settings);
  s.crc = crc32((const uint8_t*)&s, offsetof(I2Settings, crc));
}

static bool valid(const I2Settings& s) {
  if (s.magic != MAGIC || s.version != I2_SETTINGS_VERSION || s.size != sizeof(I2Settings)) return false;
  return s.crc == crc32((const uint8_t*)&s, offsetof(I2Settings, crc));
}

static void fillDefaults(I2Settings& s) {
  memset(&s, 0, sizeof(s));                  // also zeroes padding -> stable CRC / memcmp
  i2Copy(s.ssid, sizeof(s.ssid), s_def.ssid);
  i2Copy(s.wifiPass, sizeof(s.wifiPass), s_def.wifiPass);
  s.channelId = s_def.channelId;
  i2Copy(s.writeKey, sizeof(s.writeKey), s_def.writeKey);
  i2Copy(s.readKey, sizeof(s.readKey), s_def.readKey);
  s.cfgChannelId = 0;
  i2Copy(s.accessCode, sizeof(s.accessCode), s_def.accessCode);
  s.fastMode = 0;
  s.calAuto = 0;
  s.sleepSec = s_def.sleepSec;
  s.calVersion = 0;
  s.pm25 = s_def.pm25;
  s.pm10 = s_def.pm10;
  s.temp = s_def.temp;
  s.humid = s_def.humid;
  s.slmOffsetDb = 3.0103f;                   // sine RMS vs dBFS (see esp32-i2s-slm)
  seal(s);
}

static bool readSlot(const char* key, I2Settings& out) {
  if (s_prefs.getBytesLength(key) != sizeof(I2Settings)) return false;
  s_prefs.getBytes(key, &out, sizeof(out));
  return valid(out);
}

// Writes slot A, verifies it, then writes slot B.
static bool writeSlots(const I2Settings& s) {
  size_t a = s_prefs.putBytes(KEY_A, &s, sizeof(s));
  I2Settings chk;
  bool okA = (a == sizeof(s)) && readSlot(KEY_A, chk) && memcmp(&chk, &s, sizeof(s)) == 0;
  size_t b = s_prefs.putBytes(KEY_B, &s, sizeof(s));
  bool okB = (b == sizeof(s)) && readSlot(KEY_B, chk) && memcmp(&chk, &s, sizeof(s)) == 0;
  if (!okA || !okB) Serial.printf("[cfg] WRITE PROBLEM slotA=%d slotB=%d\n", okA, okB);
  return okA && okB;
}

void i2cfgBegin(const I2Defaults& d) {
  s_def = d;
  if (!s_mtx) s_mtx = xSemaphoreCreateMutex();
  s_prefs.begin(NVS_NS, false);

  I2Settings a, b;
  bool okA = readSlot(KEY_A, a);
  bool okB = readSlot(KEY_B, b);

  if (okA) {
    s_cfg = a; s_fromNvs = true;
    if (!okB) { Serial.println("[cfg] slot B damaged - repairing"); writeSlots(s_cfg); }
  } else if (okB) {
    s_cfg = b; s_fromNvs = true;
    Serial.println("[cfg] slot A damaged - restored from slot B");
    writeSlots(s_cfg);
  } else {
    Serial.println("[cfg] no valid settings in flash - using factory defaults");
    fillDefaults(s_cfg);
    s_fromNvs = false;
    writeSlots(s_cfg);
  }
}

const I2Defaults& i2cfgDefaults() { return s_def; }
bool i2cfgLoadedFromNvs() { return s_fromNvs; }

void i2cfgSnapshot(I2Settings& out) {
  xSemaphoreTake(s_mtx, portMAX_DELAY);
  out = s_cfg;
  xSemaphoreGive(s_mtx);
}

bool i2cfgCommit(const I2Settings& next) {
  I2Settings n = next;
  seal(n);
  bool ok = true;
  xSemaphoreTake(s_mtx, portMAX_DELAY);
  if (memcmp(&n, &s_cfg, sizeof(n)) != 0) {      // unchanged -> no flash write
    ok = writeSlots(n);
    if (ok) s_cfg = n;
  }
  xSemaphoreGive(s_mtx);
  return ok;
}

bool i2cfgFactoryReset() {
  I2Settings d;
  fillDefaults(d);
  return i2cfgCommit(d);
}

bool i2cfgCheckAccessCode(const char* code) {
  char stored[sizeof(s_cfg.accessCode)];
  xSemaphoreTake(s_mtx, portMAX_DELAY);
  i2Copy(stored, sizeof(stored), s_cfg.accessCode);
  xSemaphoreGive(s_mtx);
  size_t la = strlen(code), lb = strlen(stored);
  uint8_t diff = (la != lb);
  size_t n = la < lb ? la : lb;
  for (size_t i = 0; i < n; i++) diff |= (uint8_t)(code[i] ^ stored[i]);
  return diff == 0;
}

uint8_t  i2cfgFastMode()    { return s_cfg.fastMode ? 1 : 0; }
uint16_t i2cfgSleepSec()    { return s_cfg.sleepSec; }
float    i2cfgSlmOffsetDb() { return s_cfg.slmOffsetDb; }

void i2cfgGetCal(I2Calib out[4]) {
  xSemaphoreTake(s_mtx, portMAX_DELAY);
  out[0] = s_cfg.pm25; out[1] = s_cfg.pm10; out[2] = s_cfg.temp; out[3] = s_cfg.humid;
  xSemaphoreGive(s_mtx);
}

bool i2cfgApplyCal(uint32_t version, const I2Calib cal[4]) {
  I2Settings s;
  i2cfgSnapshot(s);
  s.pm25 = cal[0]; s.pm10 = cal[1]; s.temp = cal[2]; s.humid = cal[3];
  s.calVersion = version;
  return i2cfgCommit(s);
}

bool i2ValidChannelId(const String& s, uint32_t* out) {
  size_t n = s.length();
  if (n < I2_CHID_MIN_DIGITS || n > I2_CHID_MAX_DIGITS) return false;
  if (s[0] == '0') return false;
  uint32_t v = 0;
  for (size_t i = 0; i < n; i++) {
    if (!isDigit((unsigned char)s[i])) return false;
    v = v * 10 + (uint32_t)(s[i] - '0');
  }
  if (out) *out = v;
  return true;
}

bool i2ValidApiKey(const String& s) {
  if (s.length() != I2_API_KEY_LEN) return false;
  for (size_t i = 0; i < s.length(); i++)
    if (!isAlphaNumeric((unsigned char)s[i])) return false;
  return true;
}

bool i2CalSane(const I2Calib& c) {
  return (c.gain >= 0.1f && c.gain <= 10.0f) && (c.offset >= -500.0f && c.offset <= 500.0f);
}

bool i2CalParseCsv(const char* csv, uint32_t* version, I2Calib out[4]) {
  if (!csv) return false;
  unsigned long ver = 0;
  float v[8];
  int n = sscanf(csv, " %lu , %f , %f , %f , %f , %f , %f , %f , %f",
                 &ver, &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]);
  if (n != 9 || ver == 0) return false;
  for (int i = 0; i < 4; i++) {
    out[i].gain = v[2 * i];
    out[i].offset = v[2 * i + 1];
    if (!i2CalSane(out[i])) return false;
  }
  *version = (uint32_t)ver;
  return true;
}
