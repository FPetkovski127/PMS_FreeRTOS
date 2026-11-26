// serial commands of the sensor unit: perf, slm, cal
// (help, status, show, login, setup, set ... are in I2Console)
#include "app_state.h"
#include "sound_level.h"

static const char* HELP_EXT =
  "Sensor commands:\n"
  "  perf                    timing / stack / heap / CPU report\n"
  "  perf reset              clear min/max/avg statistics\n"
  "  perf cpu on|off         CPU-load sampling (1 s window, peak is kept)\n"
  "  perf load <pct> <prio> <core> <sec>   synthetic CPU load (pct 1-95) to find the limit\n"
  "  slm                     sound level meter status\n"
  "  slm on|off              start / stop the microphone processing\n"
  "  slm fft                 print one spectrum (START/END format for inmp441.py)\n"
  "  slm raw                 microphone diagnostics: are raw I2S words non-zero?\n"
  "  slm ref <dB>            (login) calibrate: mic is in front of a <dB> calibrator right now\n"
  "  cal                     show active + pending calibration\n"
  "  cal fetch               check the calibration channel now\n"
  "  cal accept|reject       (login) take / refuse the pending remote calibration\n"
  "  cal auto on|off         (login) apply remote calibration without asking\n";

static void showCal() {
  I2Settings s;
  i2cfgSnapshot(s);
  Serial.printf("Active calibration (version %lu, remote auto-apply %s):\n", (unsigned long)s.calVersion, s.calAuto ? "ON" : "off");
  Serial.printf("  PM2.5 : x %.4f + %.3f\n  PM10  : x %.4f + %.3f\n  Temp  : x %.4f + %.3f\n  Humid : x %.4f + %.3f\n",
                s.pm25.gain, s.pm25.offset, s.pm10.gain, s.pm10.offset,
                s.temp.gain, s.temp.offset, s.humid.gain, s.humid.offset);
  Serial.printf("  calibration channel: %s\n", s.cfgChannelId ? String((unsigned long)s.cfgChannelId).c_str() : "(none)");
  CalPending p;
  if (calGetPending(p)) {
    Serial.printf("PENDING remote version %lu  ->  'cal accept' or 'cal reject':\n", (unsigned long)p.version);
    Serial.printf("  PM2.5 : x %.4f + %.3f\n  PM10  : x %.4f + %.3f\n  Temp  : x %.4f + %.3f\n  Humid : x %.4f + %.3f\n",
                  p.cal[0].gain, p.cal[0].offset, p.cal[1].gain, p.cal[1].offset,
                  p.cal[2].gain, p.cal[2].offset, p.cal[3].gain, p.cal[3].offset);
  }
}

static bool handlePerf(const String& args) {
  if (args.length() == 0) { perfReport(Serial); return true; }
  if (args == "reset")   { perfReset(); Serial.println("perf statistics cleared"); return true; }
  if (args == "cpu on")  { perfCpuEnable(true);  Serial.println("CPU load sampling ON (wait a few seconds, then 'perf')"); return true; }
  if (args == "cpu off") { perfCpuEnable(false); Serial.println("CPU load sampling OFF"); return true; }
  if (args.startsWith("load ")) {
    unsigned pct = 0, prio = 0, sec = 0;
    int core = 0;
    if (sscanf(args.c_str() + 5, "%u %u %d %u", &pct, &prio, &core, &sec) == 4 &&
        perfLoadStart((uint8_t)pct, prio, core, sec)) return true;
    Serial.println("Usage: perf load <pct 1-95> <prio 0-24> <core 0|1> <seconds>");
    return true;
  }
  Serial.println("perf [reset | cpu on|off | load <pct> <prio> <core> <sec>]");
  return true;
}

static bool handleSlm(const String& args) {
  if (args.length() == 0) { slmPrint(Serial); return true; }
  if (args == "on")  { slmSetActive(true);  Serial.println("SLM started"); return true; }
  if (args == "off") { slmSetActive(false); Serial.println("SLM stopped"); return true; }
  if (args == "fft") { slmRequestFft(); return true; }
  if (args == "raw") { slmDiag(Serial); return true; }
  if (args.startsWith("ref ")) {
    if (!i2consoleRequireAuth()) return true;
    float ref = args.substring(4).toFloat();
    SlmStatus s = slmStatus();
    if (ref < 40 || ref > 120 || !s.valid) { Serial.println("Need a valid reading and a reference of 40..120 dB."); return true; }
    I2Settings c;
    i2cfgSnapshot(c);
    c.slmOffsetDb += ref - s.leq1s;
    Serial.println(i2cfgCommit(c) ? "Mic offset updated (see 'show')." : "SAVE FAILED.");
    Serial.printf("new offset %.2f dB\n", c.slmOffsetDb);
    return true;
  }
  Serial.println("slm [on | off | fft | ref <dB>]");
  return true;
}

static bool handleCal(const String& args) {
  if (args.length() == 0 || args == "show") { showCal(); return true; }
  if (args == "fetch") { calRequestFetch(); Serial.println("calibration check requested"); return true; }
  if (args == "accept") {
    if (!i2consoleRequireAuth()) return true;
    Serial.println(calAcceptPending() ? "Calibration applied and stored." : "Nothing pending (or save failed).");
    return true;
  }
  if (args == "reject") {
    if (!i2consoleRequireAuth()) return true;
    calRejectPending();
    Serial.println("Pending calibration rejected (this version will not be offered again until a newer one appears).");
    return true;
  }
  if (args == "auto on" || args == "auto off") {
    if (!i2consoleRequireAuth()) return true;
    I2Settings s;
    i2cfgSnapshot(s);
    s.calAuto = (args == "auto on");
    Serial.println(i2cfgCommit(s) ? (s.calAuto ? "Remote calibration will be applied automatically." : "Remote calibration needs confirmation (cal accept).") : "SAVE FAILED.");
    return true;
  }
  Serial.println("cal [show | fetch | accept | reject | auto on|off]");
  return true;
}

static bool sensorExt(const String& cmd, const String& args) {
  if (cmd == "perf") return handlePerf(args);
  if (cmd == "slm")  return handleSlm(args);
  if (cmd == "cal")  return handleCal(args);
  return false;
}

static void sensorStatus() {
  Serial.printf("FSM: %s  mode %s  sleep %u s  heartbeat %lu\n", pmsStateName(g_pmsState),
                i2cfgFastMode() ? "FAST" : "NORMAL", (unsigned)i2cfgSleepSec(), (unsigned long)g_pmsHeartbeat);
  if (g_tsLastOkMs) Serial.printf("ThingSpeak: last HTTP %d, last good upload %lu s ago\n", (int)g_tsLastStatus,
                                  (unsigned long)((millis() - g_tsLastOkMs) / 1000));
  else              Serial.printf("ThingSpeak: no successful upload yet (last HTTP %d)\n", (int)g_tsLastStatus);
  CalPending p;
  if (calGetPending(p)) Serial.printf("Calibration: version %lu is PENDING (type 'cal')\n", (unsigned long)p.version);
  slmPrint(Serial);
}

// a mode or sleep time change wakes the state machine
static void settingsChanged() { if (g_pmsTask) xTaskNotifyGive(g_pmsTask); }

void sensorConsoleInit() { i2consoleBegin(sensorExt, HELP_EXT, sensorStatus, settingsChanged); }
