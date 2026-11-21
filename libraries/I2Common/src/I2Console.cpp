#include "I2Console.h"
#include "I2Settings.h"
#include "I2Wifi.h"
#include "I2Perf.h"
#include <esp_system.h>
#include <stdlib.h>

static const uint32_t AUTH_TIMEOUT_MS   = 5UL * 60UL * 1000UL;
static const uint32_t LOCKOUT_MS        = 30UL * 1000UL;
static const uint32_t WIZARD_TIMEOUT_MS = 120UL * 1000UL;

static I2ConsoleExt  s_ext = nullptr;
static const char*   s_extHelp = "";
static I2ConsoleHook s_onStatus = nullptr;
static I2ConsoleHook s_onChanged = nullptr;

// ---------------------------------------------------------------------------
//  Field table (shared by 'set', 'show' and the wizard)
// ---------------------------------------------------------------------------
enum Field {
  F_SSID, F_PASS, F_CHANNEL, F_WKEY, F_RKEY, F_CFGCH, F_CFGKEY, F_MODE, F_SLEEP,
  F_PM25G, F_PM25O, F_PM10G, F_PM10O, F_TG, F_TO, F_HG, F_HO, F_SLM, F_COUNT
};
struct FieldDef { const char* key; const char* label; uint8_t roles; bool secret; };
static const FieldDef FIELDS[F_COUNT] = {
  {"ssid",   "WiFi network name (SSID)",                          3, false},
  {"pass",   "WiFi password ('-' = open network)",               3, true},
  {"channel","ThingSpeak channel ID (6-8 digits)",                3, false},
  {"wkey",   "Channel WRITE API key (16 chars)",                  1, true},
  {"rkey",   "Channel READ API key (16 chars, '-' = public)",     2, true},
  {"cfgch",  "Calibration channel ID (6-8 digits, '-' = none)",   1, false},
  {"cfgkey", "Calibration channel READ key ('-' = public)",       1, true},
  {"mode",   "Mode (normal / fast)",                              1, false},
  {"sleep",  "Sleep time in normal mode [s] (10-3600)",           1, false},
  {"pm25g",  "PM2.5 gain",                                        1, false},
  {"pm25o",  "PM2.5 offset [ug/m3]",                              1, false},
  {"pm10g",  "PM10 gain",                                         1, false},
  {"pm10o",  "PM10 offset [ug/m3]",                               1, false},
  {"tg",     "Temperature gain",                                  1, false},
  {"to",     "Temperature offset [C]",                            1, false},
  {"hg",     "Humidity gain",                                     1, false},
  {"ho",     "Humidity offset [%]",                               1, false},
  {"slmoff", "Microphone calibration offset [dB]",                1, false},
};

static uint8_t roleBit() { return (uint8_t)i2cfgDefaults().role; }

static String maskKey(const char* k) {
  size_t n = strlen(k);
  if (!n) return String("(none)");
  String s;
  for (size_t i = 0; i + 4 < n; i++) s += '*';
  s += (n > 4) ? String(k + n - 4) : String(k);
  return s;
}

static String fieldGet(const I2Settings& s, int f, bool mask) {
  switch (f) {
    case F_SSID:    return s.ssid[0] ? String(s.ssid) : String("(none)");
    case F_PASS:    return s.wifiPass[0] ? (mask ? String("********") : String(s.wifiPass)) : String("(open)");
    case F_CHANNEL: return String((unsigned long)s.channelId);
    case F_WKEY:    return mask ? maskKey(s.writeKey) : String(s.writeKey);
    case F_RKEY:    return s.readKey[0] ? (mask ? maskKey(s.readKey) : String(s.readKey)) : String("(public)");
    case F_CFGCH:   return s.cfgChannelId ? String((unsigned long)s.cfgChannelId) : String("(none)");
    case F_CFGKEY:  return s.cfgReadKey[0] ? (mask ? maskKey(s.cfgReadKey) : String(s.cfgReadKey)) : String("(public)");
    case F_MODE:    return String(s.fastMode ? "fast" : "normal");
    case F_SLEEP:   return String((unsigned)s.sleepSec);
    case F_PM25G:   return String(s.pm25.gain, 4);
    case F_PM25O:   return String(s.pm25.offset, 3);
    case F_PM10G:   return String(s.pm10.gain, 4);
    case F_PM10O:   return String(s.pm10.offset, 3);
    case F_TG:      return String(s.temp.gain, 4);
    case F_TO:      return String(s.temp.offset, 3);
    case F_HG:      return String(s.humid.gain, 4);
    case F_HO:      return String(s.humid.offset, 3);
    case F_SLM:     return String(s.slmOffsetDb, 2);
  }
  return String();
}

static bool parseFloat(const String& v, float* out) {
  char* end = nullptr;
  float x = strtof(v.c_str(), &end);
  if (end == v.c_str() || *end != 0 || isnan(x) || isinf(x)) return false;
  *out = x;
  return true;
}

static bool setCal(float* dst, const String& v, bool gain, String& err) {
  float x;
  if (!parseFloat(v, &x)) { err = "not a number"; return false; }
  if (gain && (x < 0.1f || x > 10.0f)) { err = "gain must be 0.1 .. 10"; return false; }
  if (!gain && (x < -500.0f || x > 500.0f)) { err = "offset must be -500 .. 500"; return false; }
  *dst = x;
  return true;
}

static bool fieldSet(I2Settings& s, int f, String v, String& err) {
  v.trim();
  bool clear = (v == "-");
  switch (f) {
    case F_SSID:
      if (v.length() < 1 || v.length() > 32) { err = "SSID must be 1-32 characters"; return false; }
      i2Copy(s.ssid, sizeof(s.ssid), v.c_str()); return true;
    case F_PASS:
      if (clear) { s.wifiPass[0] = 0; return true; }
      if (v.length() < 8 || v.length() > 64) { err = "WPA password must be 8-64 characters"; return false; }
      i2Copy(s.wifiPass, sizeof(s.wifiPass), v.c_str()); return true;
    case F_CHANNEL: {
      uint32_t id;
      if (!i2ValidChannelId(v, &id)) { err = "channel ID must be 6-8 digits"; return false; }
      s.channelId = id; return true;
    }
    case F_WKEY:
      if (!i2ValidApiKey(v)) { err = "write key must be exactly 16 letters/digits"; return false; }
      i2Copy(s.writeKey, sizeof(s.writeKey), v.c_str()); return true;
    case F_RKEY:
      if (clear) { s.readKey[0] = 0; return true; }
      if (!i2ValidApiKey(v)) { err = "read key must be exactly 16 letters/digits"; return false; }
      i2Copy(s.readKey, sizeof(s.readKey), v.c_str()); return true;
    case F_CFGCH: {
      if (clear) { s.cfgChannelId = 0; return true; }
      uint32_t id;
      if (!i2ValidChannelId(v, &id)) { err = "channel ID must be 6-8 digits"; return false; }
      s.cfgChannelId = id; return true;
    }
    case F_CFGKEY:
      if (clear) { s.cfgReadKey[0] = 0; return true; }
      if (!i2ValidApiKey(v)) { err = "read key must be exactly 16 letters/digits"; return false; }
      i2Copy(s.cfgReadKey, sizeof(s.cfgReadKey), v.c_str()); return true;
    case F_MODE:
      if (v.equalsIgnoreCase("fast") || v.equalsIgnoreCase("f")) { s.fastMode = 1; return true; }
      if (v.equalsIgnoreCase("normal") || v.equalsIgnoreCase("n")) { s.fastMode = 0; return true; }
      err = "type fast or normal"; return false;
    case F_SLEEP: {
      long x = v.toInt();
      if (x < 10 || x > 3600) { err = "sleep must be 10 .. 3600 s"; return false; }
      s.sleepSec = (uint16_t)x; return true;
    }
    case F_PM25G: return setCal(&s.pm25.gain, v, true, err);
    case F_PM25O: return setCal(&s.pm25.offset, v, false, err);
    case F_PM10G: return setCal(&s.pm10.gain, v, true, err);
    case F_PM10O: return setCal(&s.pm10.offset, v, false, err);
    case F_TG:    return setCal(&s.temp.gain, v, true, err);
    case F_TO:    return setCal(&s.temp.offset, v, false, err);
    case F_HG:    return setCal(&s.humid.gain, v, true, err);
    case F_HO:    return setCal(&s.humid.offset, v, false, err);
    case F_SLM: {
      float x;
      if (!parseFloat(v, &x) || x < -60 || x > 60) { err = "offset must be -60 .. 60 dB"; return false; }
      s.slmOffsetDb = x; return true;
    }
  }
  err = "unknown field";
  return false;
}

static int findField(const String& key) {
  for (int i = 0; i < F_COUNT; i++)
    if (key.equalsIgnoreCase(FIELDS[i].key) && (FIELDS[i].roles & roleBit())) return i;
  return -1;
}

// ---------------------------------------------------------------------------
//  Authentication
// ---------------------------------------------------------------------------
static bool     s_unlocked = false;
static uint32_t s_unlockedAt = 0;
static uint8_t  s_fails = 0;
static uint32_t s_lockUntil = 0;

bool i2consoleAuthed() {
  if (s_unlocked && millis() - s_unlockedAt > AUTH_TIMEOUT_MS) {
    s_unlocked = false;
    Serial.println("[console] access code expired - login again");
  }
  return s_unlocked;
}

bool i2consoleRequireAuth() {
  if (i2consoleAuthed()) return true;
  Serial.println("Locked. Use: login <access code>");
  return false;
}

static bool constEq(const char* a, const char* b) {
  size_t la = strlen(a), lb = strlen(b);
  uint8_t d = (la != lb);
  size_t n = la < lb ? la : lb;
  for (size_t i = 0; i < n; i++) d |= (uint8_t)(a[i] ^ b[i]);
  return d == 0;
}

static void doLogin(const String& code) {
  if (s_lockUntil && (int32_t)(s_lockUntil - millis()) > 0) {
    Serial.printf("Too many wrong codes - wait %lu s\n", (unsigned long)((s_lockUntil - millis()) / 1000 + 1));
    return;
  }
  I2Settings c;
  i2cfgSnapshot(c);
  if (constEq(code.c_str(), c.accessCode)) {
    s_unlocked = true; s_unlockedAt = millis(); s_fails = 0; s_lockUntil = 0;
    Serial.println("Unlocked for 5 minutes.");
  } else {
    Serial.println("Wrong access code.");
    if (++s_fails >= 3) { s_fails = 0; s_lockUntil = millis() + LOCKOUT_MS; Serial.println("Locked out for 30 s."); }
  }
}

// ---------------------------------------------------------------------------
//  Output helpers
// ---------------------------------------------------------------------------
static void changed() { if (s_onChanged) s_onChanged(); }

static void showAll(const I2Settings& s) {
  for (int f = 0; f < F_COUNT; f++) {
    if (!(FIELDS[f].roles & roleBit())) continue;
    Serial.printf("  %-7s %-50s = %s\n", FIELDS[f].key, FIELDS[f].label, fieldGet(s, f, true).c_str());
  }
  if (roleBit() & I2_ROLE_SENSOR)
    Serial.printf("  calibration version in use: %lu   auto-apply remote: %s\n",
                  (unsigned long)s.calVersion, s.calAuto ? "ON" : "off");
}

static const char* resetReasonName() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "power-on";
    case ESP_RST_EXT:       return "external reset (EN pin / button)";
    case ESP_RST_SW:        return "software restart";
    case ESP_RST_PANIC:     return "PANIC / crash";
    case ESP_RST_INT_WDT:   return "interrupt watchdog";
    case ESP_RST_TASK_WDT:  return "task watchdog";
    case ESP_RST_WDT:       return "other watchdog";
    case ESP_RST_DEEPSLEEP: return "deep sleep wake";
    case ESP_RST_BROWNOUT:  return "BROWNOUT (supply dip)";
    default:                return "other";
  }
}

static void printStatus() {
  const I2Defaults& d = i2cfgDefaults();
  Serial.printf("\n%s  fw %s   uptime %lu s   reset reason: %s\n", d.hostname, d.fwVersion,
                (unsigned long)(millis() / 1000), resetReasonName());
  i2wifiPrintStatus(Serial);
  Serial.printf("Heap free %u (min %u)   settings from flash: %s   console: %s\n",
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(),
                i2cfgLoadedFromNvs() ? "yes" : "defaults", i2consoleAuthed() ? "UNLOCKED" : "locked");
  if (s_onStatus) s_onStatus();
}

static void printHelp() {
  Serial.println(
    "\nCommands without login:\n"
    "  help | status | show\n"
    "  login <code> | logout\n"
    "Commands that need login:\n"
    "  setup            guided wizard (ENTER keeps the current value)\n"
    "  portal           open the WiFi config access point now (3 min, needs the access code on the page)\n"
    "  set <key> <val>  change one value (keys: see 'show')\n"
    "  fast | normal    change measuring mode\n"
    "  passwd <new>     change the access code (4-16 chars)\n"
    "  restart          reboot (needed after WiFi / channel changes)\n"
    "  factory yes      erase everything back to factory defaults");
  Serial.print(s_extHelp);
}

// ---------------------------------------------------------------------------
//  Wizard
// ---------------------------------------------------------------------------
static struct {
  bool active = false;
  bool confirm = false;
  int idx = 0;
  uint32_t last = 0;
  I2Settings wip;
} W;

static void wizPrompt() {
  Serial.printf("\n[%s]\n%s\n  current: %s\n  new (ENTER = keep, '-' = clear, 'cancel' = abort) > ",
                FIELDS[W.idx].key, FIELDS[W.idx].label, fieldGet(W.wip, W.idx, true).c_str());
}

static void wizNext() {
  do { W.idx++; } while (W.idx < F_COUNT && !(FIELDS[W.idx].roles & roleBit()));
  if (W.idx >= F_COUNT) {
    Serial.println("\n--- summary ---");
    showAll(W.wip);
    Serial.print("Save and restart? (y/N) > ");
    W.confirm = true;
  } else {
    wizPrompt();
  }
}

static void wizStart() {
  i2cfgSnapshot(W.wip);
  W.active = true; W.confirm = false; W.idx = -1; W.last = millis();
  Serial.println("\n=== SETUP WIZARD ===");
  wizNext();
}

static void wizLine(String line) {
  W.last = millis();
  line.trim();
  if (line.equalsIgnoreCase("cancel")) { W.active = false; Serial.println("Setup cancelled, nothing saved."); return; }

  if (W.confirm) {
    W.active = false;
    if (line.equalsIgnoreCase("y") || line.equalsIgnoreCase("yes")) {
      if (i2cfgCommit(W.wip)) {
        Serial.println("Saved. Restarting ...");
        changed();
        vTaskDelay(pdMS_TO_TICKS(800));
        ESP.restart();
      } else {
        Serial.println("SAVE FAILED (flash write/verify error) - old settings kept.");
      }
    } else {
      Serial.println("Not saved.");
    }
    return;
  }

  if (line.length() == 0) { wizNext(); return; }       // ENTER = keep current value
  String err;
  if (fieldSet(W.wip, W.idx, line, err)) {
    Serial.printf("  -> %s\n", fieldGet(W.wip, W.idx, true).c_str());
    wizNext();
  } else {
    Serial.printf("  invalid: %s\n", err.c_str());
    wizPrompt();
  }
}

// ---------------------------------------------------------------------------
//  Command dispatcher
// ---------------------------------------------------------------------------
static void handleLine(String line) {
  if (W.active) { wizLine(line); return; }
  line.trim();
  if (line.length() == 0) return;

  String cmd = line, args = "";
  int sp = line.indexOf(' ');
  if (sp > 0) { cmd = line.substring(0, sp); args = line.substring(sp + 1); args.trim(); }
  cmd.toLowerCase();

  if (cmd == "help" || cmd == "?") { printHelp(); return; }
  if (cmd == "status")             { printStatus(); return; }
  if (cmd == "show")               { I2Settings s; i2cfgSnapshot(s); Serial.println("\nCurrent settings:"); showAll(s); return; }
  if (cmd == "login")              { doLogin(args); return; }
  if (cmd == "logout")             { s_unlocked = false; Serial.println("Locked."); return; }

  if (s_ext && s_ext(cmd, args)) return;

  if (cmd == "setup")   { if (i2consoleRequireAuth()) wizStart(); return; }
  if (cmd == "portal")  { if (i2consoleRequireAuth()) { i2wifiRequestPortal(); Serial.println("Portal requested - connect to the access point from your phone."); } return; }
  if (cmd == "restart") { if (i2consoleRequireAuth()) { Serial.println("Restarting ..."); vTaskDelay(pdMS_TO_TICKS(500)); ESP.restart(); } return; }
  if (cmd == "factory") {
    if (!i2consoleRequireAuth()) return;
    if (args != "yes") { Serial.println("Type 'factory yes' to confirm."); return; }
    Serial.println(i2cfgFactoryReset() ? "Factory defaults stored. Type 'restart'." : "Factory reset FAILED.");
    changed();
    return;
  }
  if (cmd == "passwd") {
    if (!i2consoleRequireAuth()) return;
    if (args.length() < 4 || args.length() > 16 || args.indexOf(' ') >= 0) { Serial.println("Access code: 4-16 characters, no spaces."); return; }
    I2Settings s; i2cfgSnapshot(s);
    i2Copy(s.accessCode, sizeof(s.accessCode), args.c_str());
    Serial.println(i2cfgCommit(s) ? "Access code changed." : "SAVE FAILED.");
    return;
  }
  if (cmd == "fast" || cmd == "normal") {
    if (!(roleBit() & I2_ROLE_SENSOR)) { Serial.println("Not available on this unit."); return; }
    if (!i2consoleRequireAuth()) return;
    I2Settings s; i2cfgSnapshot(s);
    s.fastMode = (cmd == "fast");
    Serial.println(i2cfgCommit(s) ? (s.fastMode ? "FAST mode" : "NORMAL mode") : "SAVE FAILED.");
    changed();
    return;
  }
  if (cmd == "set") {
    if (!i2consoleRequireAuth()) return;
    int sp2 = args.indexOf(' ');
    if (sp2 < 0) { Serial.println("Usage: set <key> <value>   (see 'show' for keys)"); return; }
    int f = findField(args.substring(0, sp2));
    if (f < 0) { Serial.println("Unknown key (see 'show')."); return; }
    I2Settings s; i2cfgSnapshot(s);
    String err;
    if (!fieldSet(s, f, args.substring(sp2 + 1), err)) { Serial.printf("Invalid: %s\n", err.c_str()); return; }
    if (i2cfgCommit(s)) {
      Serial.printf("Saved: %s = %s", FIELDS[f].key, fieldGet(s, f, true).c_str());
      Serial.println((f <= F_RKEY) ? "   (type 'restart' to apply)" : "");
      changed();
    } else {
      Serial.println("SAVE FAILED (flash write/verify error) - old settings kept.");
    }
    return;
  }
  Serial.println("Unknown command. Type 'help'.");
}

// ---------------------------------------------------------------------------
static void consoleTask(void*) {
  String line;
  char prev = 0;
  for (;;) {
    while (Serial.available()) {
      char c = (char)Serial.read();
      if (c == '\n' && prev == '\r') { prev = c; continue; }     // CRLF = one line end
      prev = c;
      if (c == '\r' || c == '\n') { handleLine(line); line = ""; }
      else if (c == 8 || c == 127) { if (line.length()) line.remove(line.length() - 1); }
      else if (c >= 32 && line.length() < 128) line += c;
    }
    if (W.active && millis() - W.last > WIZARD_TIMEOUT_MS) {
      W.active = false;
      Serial.println("\nSetup timed out, nothing saved.");
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void i2consoleBegin(I2ConsoleExt ext, const char* extHelp, I2ConsoleHook onStatus, I2ConsoleHook onSettingsChanged) {
  s_ext = ext;
  s_extHelp = extHelp ? extHelp : "";
  s_onStatus = onStatus;
  s_onChanged = onSettingsChanged;
  i2CreateTask(consoleTask, "console", 5120, NULL, 1, NULL, I2_CORE_PRO);
  Serial.println("[console] type 'help'");
}
