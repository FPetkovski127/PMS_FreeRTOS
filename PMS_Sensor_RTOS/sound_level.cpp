// ============================================================================
//  Sound level meter (INMP441 on I2S) - based on the method of
//  https://github.com/ikostoski/esp32-i2s-slm (GPL-3.0, (c)2019 Ivan Kostoski)
//
//   mic_reader (high prio)   i2s_read 125 ms block (6000 samples @ 48 kHz)
//                            -> int24 -> float -> mic equalizer IIR -> A-weighting IIR
//                            -> only two sums of squares leave the task
//   slm_calc  (low prio)     sums -> dB, 1 s Leq, overload / noise-floor flags,
//                            energy average + max for the ThingSpeak window,
//                            optional FFT on request
//
//  dB maths:  L = offset + 94 + 10*log10( meanSquare / refSquare )
//             (identical to 20*log10(RMS/ref) - the 10*log10 form on the
//              POWER (mean square) saves one sqrt() per block)
//  Levels are averaged in the ENERGY domain (10^(L/10)), never as dB values.
// ============================================================================
#include "app_config.h"

#if ENABLE_SLM
#include <driver/i2s.h>
#include <esp_idf_version.h>
#include "sos-iir-filter.h"
#include "sound_level.h"
#include "app_state.h"
#if SLM_ENABLE_FFT
#include <arduinoFFT.h>
#endif

// ----------------------------------------------------------- configuration --
#define SLM_SAMPLE_RATE   48000                    // fixed by the IIR filter design
#define SLM_SAMPLE_BITS   32
#define SAMPLES_SHORT     (SLM_SAMPLE_RATE / 8)    // 125 ms block
#define DMA_BANK_SIZE     (SAMPLES_SHORT / 16)
#define DMA_BANKS         32
#define LEQ_PERIOD_S      1
#define I2S_PORT          I2S_NUM_0
#define FFT_N             512

// INMP441 datasheet values
#define MIC_SENSITIVITY   -26.0                    // dBFS at 94 dB SPL
#define MIC_REF_DB        94.0
#define MIC_OVERLOAD_DB   120.0                    // acoustic overload point
#define MIC_NOISE_DB      33.0                     // noise floor (61 dB SNR)
#define MIC_BITS          24

// ----------------------------------------------------------------- filters --
// INMP441 equalizer (Fs = 48 kHz)
static SOS_IIR_Filter FILT_MIC(1.00197834654696f, {
  {-1.986920458344451f, +0.986963226946616f, +1.995178510504166f, -0.995184322194091f}
});

// A-weighting (Dr. Matt L., https://dsp.stackexchange.com/a/36122), Fs = 48 kHz
static SOS_IIR_Filter FILT_A(0.169994948147430f, {
  {-2.00026996133106f,  +1.00027056142719f,  -1.060868438509278f, -0.163987445885926f},
  {+4.35912384203144f,  +3.09120265783884f,  +1.208419926363593f, -0.273166998428332f},
  {-0.70930303489759f,  -0.29071868393580f,  +1.982242159753048f, -0.982298594928989f}
});

// ------------------------------------------------------------------- state --
struct SlmBlock { float sumSqrSPL; float sumSqrW; uint32_t procUs; };

static QueueHandle_t     s_q = nullptr;
static SemaphoreHandle_t s_winMtx = nullptr;
static float             s_samples[SAMPLES_SHORT] __attribute__((aligned(4)));
static double            s_refSq = 1.0;

static volatile bool  s_active = true;
static volatile bool  s_valid = false, s_overload = false, s_low = false;
static volatile float s_leq = 0, s_short = 0;

static double   s_winEnergy = 0;
static uint32_t s_winCount = 0, s_winOverloads = 0;
static float    s_winMax = -1000.0f;

static PerfProbe   pr_slmBlock = PERF_PROBE_INIT("slm_block", 125000);   // budget = block period
static PerfProbe   pr_slmCalc  = PERF_PROBE_INIT("slm_calc", 125000);
static PerfProbe   pr_slmFft   = PERF_PROBE_INIT("slm_fft", 0);
static PerfCounter ct_slmDrop  = PERF_COUNTER_INIT("slm_block_dropped");

// ---- diagnostics for 'slm raw' (why is the level at the minimum?) ----
static esp_err_t     s_errInstall = ESP_OK, s_errPins = ESP_OK;
static volatile bool s_dbgReq = false, s_dbgReady = false;
static int32_t       s_dbgMin = 0, s_dbgMax = 0, s_dbgFirst[4] = {0, 0, 0, 0};
static float         s_dbgSumSPL = 0, s_dbgSumW = 0;
static volatile uint32_t s_blocks = 0;

#if SLM_ENABLE_FFT
static volatile bool s_fftWanted = false, s_fftReady = false;
static float  s_fftIn[FFT_N];
static double s_vReal[FFT_N], s_vImag[FFT_N];
static ArduinoFFT<double> s_fft = ArduinoFFT<double>(s_vReal, s_vImag, FFT_N, (double)SLM_SAMPLE_RATE);
#endif

static double dbOf(double meanSq, double offsetDb) {
  double db = offsetDb + MIC_REF_DB + 10.0 * log10(meanSq / s_refSq);
  return (db > -200.0) ? db : -200.0;               // also catches log10(0) = -inf
}

// -------------------------------------------------------------------- I2S ---
static void micInit() {
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
  cfg.sample_rate = SLM_SAMPLE_RATE;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  cfg.channel_format = SLM_I2S_CHANNEL;                      // see app_config.h
  cfg.communication_format = I2S_COMM_FORMAT_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = DMA_BANKS;
  cfg.dma_buf_len = DMA_BANK_SIZE;
  cfg.use_apll = SLM_USE_APLL;
  cfg.tx_desc_auto_clear = false;
  cfg.fixed_mclk = 0;

  i2s_pin_config_t pins = {};
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 0)
  pins.mck_io_num = I2S_PIN_NO_CHANGE;                      // do not drive GPIO0 as MCLK
#endif
  pins.bck_io_num = PIN_I2S_SCK;
  pins.ws_io_num = PIN_I2S_WS;
  pins.data_out_num = I2S_PIN_NO_CHANGE;
  pins.data_in_num = PIN_I2S_SD;

  s_errInstall = i2s_driver_install(I2S_PORT, &cfg, 0, NULL);
  s_errPins = i2s_set_pin(I2S_PORT, &pins);
  Serial.printf("[slm] i2s install=%d set_pin=%d (0 = OK)  apll=%d\n", (int)s_errInstall, (int)s_errPins, (int)SLM_USE_APLL);
}

// ------------------------------------------------------------ reader task ---
static void slmReaderTask(void*) {
  micInit();
  size_t br = 0;
  i2s_read(I2S_PORT, s_samples, sizeof(s_samples), &br, portMAX_DELAY);   // mic start-up block (INMP441 up to 83 ms)
  bool wasActive = true;

  for (;;) {
    if (!s_active) {
      if (wasActive) { i2s_stop(I2S_PORT); wasActive = false; }
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    if (!wasActive) {
      i2s_zero_dma_buffer(I2S_PORT);
      i2s_start(I2S_PORT);
      i2s_read(I2S_PORT, s_samples, sizeof(s_samples), &br, portMAX_DELAY);
      wasActive = true;
    }

    i2s_read(I2S_PORT, s_samples, SAMPLES_SHORT * sizeof(int32_t), &br, portMAX_DELAY);   // blocks ~125 ms
    if (br != SAMPLES_SHORT * sizeof(int32_t)) continue;

    uint32_t t0 = perfNowUs();                       // -------- measured section --------
    if (s_dbgReq) {                                  // 'slm raw': statistics of the RAW words
      int32_t* raw = (int32_t*)s_samples;
      int32_t mn = raw[0], mx = raw[0];
      for (int i = 0; i < SAMPLES_SHORT; i++) { if (raw[i] < mn) mn = raw[i]; if (raw[i] > mx) mx = raw[i]; }
      s_dbgMin = mn; s_dbgMax = mx;
      for (int i = 0; i < 4; i++) s_dbgFirst[i] = raw[i];
      s_dbgReady = true;
      s_dbgReq = false;
    }
    for (int i = 0; i < SAMPLES_SHORT; i++) {        // int32 (24 valid bits) -> float, in place
      int32_t v;
      memcpy(&v, &s_samples[i], sizeof(v));
      s_samples[i] = (float)(v >> (SLM_SAMPLE_BITS - MIC_BITS));
    }
#if SLM_ENABLE_FFT
    if (s_fftWanted) {
      memcpy(s_fftIn, s_samples, sizeof(s_fftIn));   // raw copy for the spectrum
      s_fftWanted = false;
      s_fftReady = true;
    }
#endif
    SlmBlock q;
    q.sumSqrSPL = FILT_MIC.filter(s_samples, s_samples, SAMPLES_SHORT);   // equalized, unweighted
    q.sumSqrW   = FILT_A.filter(s_samples, s_samples, SAMPLES_SHORT);     // A-weighted
    q.procUs = perfNowUs() - t0;                     // ------------------------------------
    s_dbgSumSPL = q.sumSqrSPL;
    s_dbgSumW = q.sumSqrW;
    s_blocks++;
    perfRecord(&pr_slmBlock, q.procUs);

    if (xQueueSend(s_q, &q, 0) != pdTRUE) perfCount(ct_slmDrop);   // calc task too slow
  }
}

// -------------------------------------------------------------- calc task ---
#if SLM_ENABLE_FFT
static void runFft() {
  PerfScope scope(pr_slmFft);
  for (int i = 0; i < FFT_N; i++) { s_vReal[i] = (double)((int32_t)s_fftIn[i] >> 6); s_vImag[i] = 0.0; }
  s_fft.windowing(s_vReal, FFT_N, FFT_WIN_TYP_HAMMING, FFT_FORWARD);
  s_fft.compute(s_vReal, s_vImag, FFT_N, FFT_FORWARD);
  s_fft.complexToMagnitude(s_vReal, s_vImag, FFT_N);
  s_fftReady = false;
}

static void printFft() {                             // same format as INMP441_spl.ino -> inmp441.py
  Serial.println("START");
  for (int i = 1; i < FFT_N / 2; i++) Serial.printf("%.2f,%.3f\n", (double)i * SLM_SAMPLE_RATE / FFT_N, s_vReal[i]);
  Serial.println("END");
}
#endif

static void slmCalcTask(void*) {
  SlmBlock q;
  double leqSum = 0;
  uint32_t leqN = 0;
  bool ovl = false;
  float maxShort = -1000.0f;

  for (;;) {
    if (xQueueReceive(s_q, &q, pdMS_TO_TICKS(1000)) != pdTRUE) continue;
    uint32_t t0 = perfNowUs();
    const double off = i2cfgSlmOffsetDb();

    double shortZ = dbOf(q.sumSqrSPL / SAMPLES_SHORT, off);
    double shortW = dbOf(q.sumSqrW / SAMPLES_SHORT, off);
    s_short = (float)shortW;
    if (shortZ > MIC_OVERLOAD_DB) ovl = true;
    if (shortW > maxShort) maxShort = (float)shortW;

    leqSum += q.sumSqrW;
    leqN += SAMPLES_SHORT;
    if (leqN >= (uint32_t)SLM_SAMPLE_RATE * LEQ_PERIOD_S) {
      double leq = dbOf(leqSum / leqN, off);
      s_leq = (float)leq;
      s_overload = ovl;
      s_low = !(leq >= MIC_NOISE_DB);
      s_valid = true;

      xSemaphoreTake(s_winMtx, portMAX_DELAY);
      if (ovl) {
        s_winOverloads++;                            // overloaded seconds are not trusted
      } else {
        s_winEnergy += pow(10.0, leq / 10.0);
        s_winCount++;
        if (maxShort > s_winMax) s_winMax = maxShort;
      }
      xSemaphoreGive(s_winMtx);

      leqSum = 0; leqN = 0; ovl = false; maxShort = -1000.0f;
    }
    perfRecord(&pr_slmCalc, perfNowUs() - t0);

#if SLM_ENABLE_FFT
    if (s_fftReady) { runFft(); printFft(); }
#endif
  }
}

// -------------------------------------------------------------- public API --
void slmBegin() {
  s_refSq = pow(10.0, MIC_SENSITIVITY / 20.0) * (double)((1 << (MIC_BITS - 1)) - 1);
  s_refSq *= s_refSq;
  s_q = xQueueCreate(8, sizeof(SlmBlock));
  s_winMtx = xSemaphoreCreateMutex();
  perfAddProbe(&pr_slmBlock);
  perfAddProbe(&pr_slmCalc);
#if SLM_ENABLE_FFT
  perfAddProbe(&pr_slmFft);
#endif
  perfAddCounter(&ct_slmDrop);
  i2CreateTask(slmReaderTask, "slm_reader", 4096, NULL, PRIO_SLM_READER, NULL, CORE_SLM);
  i2CreateTask(slmCalcTask,   "slm_calc",   5120, NULL, PRIO_SLM_CALC,   NULL, CORE_SLM);
}

void slmSetActive(bool on) { s_active = on; if (!on) s_valid = false; }
bool slmIsActive() { return s_active; }

SlmStatus slmStatus() {
  SlmStatus s;
  s.running = s_active;
  s.valid = s_active && s_valid;
  s.leq1s = s_leq;
  s.lshort = s_short;
  s.overload = s_overload;
  s.low = s_low;
  return s;
}

bool slmTakeWindow(float* laeq, float* lamax) {
  bool ok = false;
  xSemaphoreTake(s_winMtx, portMAX_DELAY);
  if (s_winCount > 0) {
    *laeq = (float)(10.0 * log10(s_winEnergy / s_winCount));
    *lamax = s_winMax;
    ok = true;
  }
  s_winEnergy = 0; s_winCount = 0; s_winOverloads = 0; s_winMax = -1000.0f;
  xSemaphoreGive(s_winMtx);
  return ok;
}

void slmRequestFft() {
#if SLM_ENABLE_FFT
  s_fftWanted = true;
#endif
}

void slmPrint(Print& out) {
  SlmStatus s = slmStatus();
  out.printf("SLM: %s  LAeq(1s) %.1f dBA  LAeq(125ms) %.1f dBA  overload:%d low:%d  mic offset %.2f dB\n",
             s.running ? "running" : "stopped", s.leq1s, s.lshort, s.overload, s.low, i2cfgSlmOffsetDb());
  out.printf("     window since last upload: %lu valid seconds, %lu overloaded\n",
             (unsigned long)s_winCount, (unsigned long)s_winOverloads);
}

// 'slm raw': is the microphone delivering data at all?
void slmDiag(Print& out) {
  out.printf("I2S driver: install=%d set_pin=%d (0 = OK), blocks processed: %lu\n",
             (int)s_errInstall, (int)s_errPins, (unsigned long)s_blocks);
  s_dbgReady = false;
  s_dbgReq = true;
  for (int i = 0; i < 40 && !s_dbgReady; i++) vTaskDelay(pdMS_TO_TICKS(25));   // wait for the next block
  if (!s_dbgReady) { out.println("no block received in 1 s: i2s_read does not return -> check SCK/WS wiring and the I2S install result"); return; }
  out.printf("raw 32-bit words: min %ld  max %ld  first four: %08lX %08lX %08lX %08lX\n",
             (long)s_dbgMin, (long)s_dbgMax, (unsigned long)s_dbgFirst[0], (unsigned long)s_dbgFirst[1],
             (unsigned long)s_dbgFirst[2], (unsigned long)s_dbgFirst[3]);
  out.printf("sum of squares: equalised %g   A-weighted %g\n", (double)s_dbgSumSPL, (double)s_dbgSumW);
  if (s_dbgMin == 0 && s_dbgMax == 0)
    out.println("-> ALL ZERO: the microphone sends nothing in the selected slot. Check SD pin, L/R pin (GND = left, 3.3 V = right) and try the other SLM_I2S_CHANNEL.");
  else if (isnan(s_dbgSumW) || isnan(s_dbgSumSPL))
    out.println("-> raw data are fine but the filter result is NaN: filter problem, not the microphone.");
  else
    out.println("-> the microphone delivers data. If the level is still wrong the problem is in the calculation.");
}
#endif // ENABLE_SLM
