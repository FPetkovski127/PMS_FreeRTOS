"""Builds Report 2: digital sound level meter with INMP441 (DSP + FreeRTOS), draft."""
import os
from docx_helpers import Report

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "SoundLevelMeter_DSP_Report_DRAFT_v0.2.docx")

R = Report("Digital sound level meter with INMP441 and ESP32 - I2S, IIR filters, FFT and FreeRTOS (draft)")

# =============================================================================
R.title_page(
    "Digital Sound Level Meter\nwith INMP441 and ESP32",
    "I2S acquisition, IIR filtering (A-weighting) and FFT under FreeRTOS",
    ["Technical documentation - DRAFT v0.2",
     "Subjects: Digital Signal Processing / Real-Time Operating Systems",
     "Author: [name, index number]",
     "Mentor: [name]",
     "Date: 16.11.2026"],
    abstract="This report describes the sound level measurement that was added to the air-quality sensor unit. A digital MEMS microphone "
             "(INMP441) delivers 24-bit samples over I2S at 48 kHz. The samples are equalised and A-weighted with cascaded second-order IIR "
             "sections, the energy is accumulated and converted to the equivalent continuous sound level LAeq in dBA, which is uploaded to "
             "ThingSpeak. An FFT of 512 samples is available on request to identify tones. The processing is divided into FreeRTOS tasks "
             "and its execution time is measured against the real-time deadline of one audio block (125 ms). The method is based on the "
             "open-source project esp32-i2s-slm by Ivan Kostoski; the report explains the theory, how the filter coefficients are obtained and verified, "
             "what was added in this project, and how the result is validated.")
R.toc()

# =============================================================================
R.h1("Introduction")
R.h2("Motivation")
R.p("Noise is an important part of the indoor environment of a school, next to the air quality. The sensor unit that measures particulate "
    "matter, temperature and humidity (see the first report) therefore also measures the **equivalent continuous A-weighted sound level LAeq**. "
    "The task is interesting from two engineering points of view: it is a digital signal processing problem (sampling, filtering, spectrum analysis) "
    "and a real-time problem (a continuous stream of audio samples must be processed within a fixed deadline while the rest of the firmware keeps running).", align="justify")
R.h2("Goals")
R.bullets([
    "acquire audio from a digital I2S MEMS microphone with the ESP32 I2S peripheral and DMA,",
    "equalise the microphone response and apply the A-weighting with IIR filters, and compute LAeq and the maximum level,",
    "understand and verify how the filter coefficients are obtained,",
    "add an **FFT** to display the spectrum and identify tones,",
    "run everything as FreeRTOS tasks next to the particulate-matter firmware and **measure** the execution time and the processor load,",
    "calibrate the result and estimate the measurement uncertainty.",
])
R.h2("Source of the method")
R.p("The processing chain follows the open-source project **esp32-i2s-slm** by Ivan Kostoski (GPL-3.0, https://github.com/ikostoski/esp32-i2s-slm): "
    "filters in second-order-section form, an assembly-optimised filter routine, and the split into a reader task and a calculation task. "
    "The project code was adapted: INMP441 constants, A-weighting, a portable fallback of the filter routine for chips without the Xtensa FPU, "
    "run-time calibration, integration with the state machine and ThingSpeak, an FFT on request, and timing probes.", align="justify")
R.figure("slm_chain", "Signal chain of the sound level meter.")

# =============================================================================
R.h1("Theoretical background")
R.h2("Sound pressure and the decibel")
R.p("Sound is a small variation of the air pressure p(t). Because the range of audible pressures is huge (about 20 µPa at the threshold of hearing "
    "to 20 Pa at the threshold of pain), a logarithmic scale is used. The **sound pressure level** is", align="justify")
R.equation("L = 20 · log10( p_rms / p0 ),   p0 = 20 µPa", "1")
R.p("Since power (energy) is proportional to the square of the pressure, the same level can be written with the mean square:", align="justify")
R.equation("L = 10 · log10( p_rms² / p0² )", "2")
R.p("The two forms are identical (20·log10 x = 10·log10 x²). The firmware uses the second form because a square root per block is saved. "
    "Levels in decibels cannot be added or averaged directly; they must first be converted back to energy: two equal sources give +3 dB, "
    "and the average of several levels is", align="justify")
R.equation("L_avg = 10 · log10( (1/N) · Σ 10^(L_i / 10) )", "3")
R.h2("Frequency weighting")
R.p("The human ear is less sensitive to low and very high frequencies than to the middle range. Sound level meters therefore apply a **frequency weighting**. "
    "**A-weighting** approximates the sensitivity at moderate levels and is the standard for environmental and workplace noise (the result is given in dBA). "
    "C-weighting is flatter and used for high levels and peaks; Z-weighting means no weighting (flat). "
    "The A-weighting is defined in IEC 61672-1 by four pole frequencies (20.6 Hz twice, 107.7 Hz, 737.9 Hz and 12 194 Hz twice) and four zeros at 0 Hz, "
    "normalised to 0 dB at 1 kHz:", align="justify")
R.equation("H_A(s) = K · s⁴ / [ (s+ω1)² (s+ω2) (s+ω3) (s+ω4)² ]", "4")
R.table(["Frequency [Hz]", "20", "100", "1000", "4000", "8000", "16000"], [
    ["A-weighting [dB]", "-50.4", "-19.1", "0.0", "+1.0", "-1.1", "-6.7"],
], widths=[3, 1.5, 1.5, 1.5, 1.5, 1.5, 1.5], caption="A-weighting at selected frequencies (analytic IEC 61672 curve).", align_center_cols=(1, 2, 3, 4, 5, 6))
R.h2("Equivalent level and time weighting")
R.p("The **equivalent continuous sound level** over a period T is the level of a steady sound that contains the same energy:", align="justify")
R.equation("L_Aeq,T = 10 · log10( (1/T) ∫ p_A²(t) dt / p0² )  ≈  10 · log10( (1/N) Σ x_A[n]² / ref² )", "5")
R.p("The second form is what the firmware computes: the sum of squares of the A-weighted samples divided by the number of samples. "
    "Classic meters also offer time weightings (Fast = 125 ms, Slow = 1 s exponential averaging) and peak detection. "
    "Here the signal is processed in blocks of 125 ms; the level of one block is used as a short-term value (and its maximum as LAmax), and the "
    "level over 1 s is the Leq. This is similar to, but not identical with, the “Fast” time weighting of a Class 1 or 2 instrument.", align="justify")
R.table(["Situation", "Typical level [dBA]"], [
    ["quiet room at night", "30 - 35"],
    ["classroom without activity", "35 - 45"],
    ["normal conversation (1 m)", "55 - 65"],
    ["classroom during lesson / group work", "60 - 75"],
    ["busy street", "70 - 85"],
    ["hearing damage risk (long exposure)", "> 85"],
], widths=[7, 4], caption="Orientation values (general knowledge, to be replaced with cited values).")
R.h2("MEMS microphone INMP441")
R.p("A MEMS microphone consists of a silicon membrane that moves with the sound pressure, an amplifier and, in digital types, a sigma-delta ADC with a "
    "decimation filter. The **INMP441** is an omnidirectional microphone with an I2S output: no analogue signal path, no external ADC and "
    "little sensitivity to interference. The datasheet values that are used in the code:", align="justify")
R.table(["Parameter", "Value", "Used as"], [
    ["Sensitivity", "-26 dBFS at 94 dB SPL, 1 kHz", "`MIC_SENSITIVITY`, reference of the dB scale"],
    ["Signal-to-noise ratio", "61 dBA (re 94 dB SPL)", "noise floor about 33 dBA (`MIC_NOISE_DB`)"],
    ["Acoustic overload point (AOP)", "120 dB SPL", "`MIC_OVERLOAD_DB`"],
    ["Data format", "24 bit, two's complement, in a 32-bit I2S slot", "shift by 8 bits"],
    ["Frequency response", "flat from about 100 Hz to 15 kHz, roll-off at low frequencies", "equalizer filter"],
    ["Sampling", "I2S, clock 64 x fs, fs typically 7.8 - 50 kHz", "48 kHz"],
], widths=[3.3, 4.8, 4.5], caption="INMP441 parameters used in the firmware (typical values; verify with the datasheet).")
R.figure("level_ranges", "Mapping between acoustic level (dB SPL) and digital level (dBFS) for the INMP441.")
R.p("The mapping in the figure is L_SPL = 120 + dBFS: the digital full scale corresponds to the acoustic overload point of 120 dB SPL, the reference "
    "(94 dB SPL) lies at -26 dBFS and the noise floor at about 33 dB SPL. The usable range is therefore about 87 dB. "
    "The 24 bits themselves offer a theoretical dynamic range of 6.02·24 + 1.76 ≈ 146 dB, far more than the microphone can deliver.", align="justify")
R.h2("The I2S interface")
R.p("I2S (Inter-IC Sound) is a three-wire serial bus for digital audio: **SCK/BCLK** (bit clock), **WS/LRCLK** (word select, marks the left or right channel) "
    "and **SD** (serial data). The ESP32 is the master and generates BCLK and WS; the microphone only sends data. The data word follows the WS edge "
    "with a delay of one bit clock (the MSB appears on the second BCLK edge). The INMP441 uses a 32-bit slot per channel of which 24 bits are data; "
    "the L/R pin selects the channel (to GND = left). With a sampling rate of 48 kHz the bit clock is 48 000 x 64 = 3.072 MHz and the data rate is "
    "48 000 x 4 bytes = 192 kB/s.", align="justify")
R.figure("i2s_timing", "I2S signals (schematic): the left channel is selected while WS is low; the MSB follows one BCLK after the WS edge.")
R.p("The ESP32 I2S peripheral writes the samples with **DMA** into a ring of buffers in RAM without the CPU. The driver call `i2s_read()` blocks the calling "
    "task until the requested number of samples is available; during that time the task consumes no processor time. This is the natural place "
    "where the RTOS makes the system efficient.", align="justify")
R.h2("Sampling and quantisation")
R.p("According to the sampling theorem a signal can be reconstructed if the sampling rate is more than twice the highest frequency: at fs = 48 kHz frequencies up to "
    "24 kHz are represented, which covers the audio band (20 Hz - 20 kHz). The sigma-delta converter inside the microphone includes the anti-aliasing "
    "filter. The sampling rate is **fixed at 48 kHz** because the filter coefficients are valid only for that rate: a digital filter is defined for a given sampling "
    "frequency, and using another one shifts every frequency of the response.", align="justify")
R.h2("Digital filters")
R.h3("Difference equation and transfer function")
R.p("A linear time-invariant digital filter computes the output from the present and past inputs and past outputs:", align="justify")
R.equation("y[n] = Σ b_k · x[n−k]  −  Σ a_k · y[n−k]", "6")
R.p("Its transfer function in the z-domain is a ratio of two polynomials, H(z) = B(z)/A(z). The roots of the numerator are the **zeros**, those of the "
    "denominator the **poles**. The filter is stable if all poles lie inside the unit circle. The frequency response follows from "
    "z = e^(jΩ), Ω = 2π·f/fs.", align="justify")
R.table(["", "FIR", "IIR"], [
    ["Feedback", "none (only past inputs)", "yes (past outputs)"],
    ["Stability", "always stable", "poles must be inside the unit circle"],
    ["Phase", "can be exactly linear", "non-linear"],
    ["Order for a given magnitude response", "high (tens to hundreds of taps)", "low (a few sections)"],
    ["Cost per sample", "one multiply per tap", "five multiplications per section"],
    ["Typical use here", "-", "A-weighting and equalizer (magnitude is all that matters)"],
], widths=[4, 4, 4.6], caption="FIR and IIR filters.")
R.p("For a level measurement only the **magnitude** response matters, not the phase. IIR filters are therefore the economical choice: the A-weighting "
    "is realised with six poles and zeros, i.e. three second-order sections.", align="justify")
R.h3("Second-order sections")
R.p("A high-order IIR filter in a single polynomial form is numerically sensitive: a tiny rounding error of a coefficient moves the poles a lot and may make the "
    "filter unstable. Therefore the transfer function is factored into a **cascade of second-order sections** (biquads), each with one pair of poles and zeros:", align="justify")
R.equation("H(z) = g · Π_i ( 1 + b1_i z⁻¹ + b2_i z⁻² ) / ( 1 − a1_i z⁻¹ − a2_i z⁻² )", "7")
R.p("The code (`sos-iir-filter.h`) uses b0 = 1 and a0 = 1 and one overall gain g applied in the last section. Note the sign convention: the stored a1 and a2 are "
    "**the negatives** of the classical denominator coefficients, so that the recursion can be written with additions only.", align="justify")
R.figure("biquad", "One biquad in Direct Form II as implemented in the filter routine.")
R.code("""t[n] = x[n] + a1*w0 + a2*w1          // recursive part (poles)
y[n] = t[n] + b1*w0 + b2*w1          // feed-forward part (zeros), b0 = 1
w1 = w0;  w0 = t[n];                 // update the two state variables""")
R.p("Each section needs 4 multiplications and 4 additions per sample and two state variables. The meter uses 1 section for the microphone "
    "equaliser and 3 for the A-weighting: 4 sections, i.e. 16 multiplications and 16 additions per sample, plus the squaring and the accumulation.", align="justify")
R.h2("Filter design and the origin of the coefficients")
R.h3("Bilinear transform and frequency warping")
R.p("A standard way to design a digital filter is to start with the analogue prototype (here the IEC A-weighting, equation 4) and map it to the z-domain "
    "with the **bilinear transform** s = 2·fs·(z−1)/(z+1). The mapping compresses the infinite analogue frequency axis into 0 ... fs/2 and therefore "
    "**warps** the frequency scale: the higher the frequency, the larger the error. At fs = 48 kHz the poles near 12 kHz are strongly displaced.", align="justify")
R.h3("Direct digital design by curve fitting")
R.p("The project filters were designed in the digital domain: the desired magnitude response is specified at several frequencies and a rational transfer function is "
    "**fitted** to these points (GNU Octave function `invfreqz`, which finds the numerator and denominator of a given order that best matches a given complex "
    "frequency response). Because the fit is done at the sampling rate of the filter there is no warping. The result is checked and stabilised "
    "(`polystab` reflects poles outside the unit circle), and finally converted to second-order sections with `tf2sos`.", align="justify")
R.table(["f [Hz]", "20", "100", "1000", "4000", "8000", "10000", "16000", "20000"], [
    ["analytic A-weighting [dB]", "-50.4", "-19.1", "0.0", "+0.96", "-1.15", "-2.49", "-6.71", "-9.35"],
    ["bilinear transform [dB]", "-50.4", "-19.2", "0.0", "+0.93", "-1.69", "-3.71", "-13.14", "-25.19"],
    ["project IIR (direct fit) [dB]", "-50.1", "-19.1", "0.0", "+0.95", "-1.14", "-2.46", "-6.75", "-9.37"],
], widths=[3.4, 1.2, 1.2, 1.2, 1.2, 1.2, 1.2, 1.2, 1.2], caption="Comparison of design methods (computed with NumPy from the analytic formula and from the coefficients in the firmware).", font=8, align_center_cols=tuple(range(1, 9)))
R.figure("a_weight_methods", "A-weighting at 48 kHz: plain bilinear transform against the direct digital fit used in the project.")
R.p("The comparison shows why the coefficients of the project cannot simply be replaced by a textbook bilinear design: the plain bilinear version is "
    "6 dB off at 16 kHz and 16 dB at 20 kHz, while the project filter stays within **0.28 dB of the analytic curve between 20 Hz and 16 kHz** and within 0.02 dB between 100 Hz and 8 kHz "
    "(values computed from the coefficient tables in Appendix A). With pre-warping or higher-rate design the bilinear method can also be improved.", align="justify")
R.figure("filter_responses", "Frequency responses of the two filters used: A-weighting (left) and microphone equalizer (right).")
R.h3("Microphone equalizer")
R.p("The datasheet shows a low-frequency roll-off of the INMP441 (about -1.8 dB at 100 Hz, -4 dB at 50 Hz and -13 dB at 20 Hz). The equaliser is the **inverse** of this response: "
    "the roll-off was approximated by a second-order IIR fit from points read from the datasheet plot (20 Hz: -13 dB, 50 Hz: -4.1 dB, 100 Hz: -1.8 dB, 1 kHz: 0 dB), "
    "the model was stabilised, and the equaliser was obtained by **swapping numerator and denominator**. The resulting single section has a gain of +11.9 dB at 20 Hz, "
    "+1.6 dB at 100 Hz and 0 dB above about 200 Hz (computed from the coefficients). The datasheet curve was read visually, which limits the accuracy at the lowest frequencies.", align="justify")
R.callout("The exact datasheet points and the fitting steps are taken from the reference project (`math/inmp441.m`). For the report the fitting should be repeated "
          "with the author's own Octave/Python script and the result compared with the coefficients in Appendix A.", "todo")
R.h2("Spectrum analysis with the FFT")
R.p("The **discrete Fourier transform** (DFT) of N samples gives N complex values, each describing the amplitude and phase at the frequency k·fs/N:", align="justify")
R.equation("X[k] = Σ_{n=0}^{N−1} x[n] · e^(−j2π·k·n/N)", "8")
R.p("The **FFT** computes it with about (N/2)·log2(N) butterfly operations instead of N²: for N = 512 about 2300 butterflies instead of 262 000 complex multiplications.", align="justify")
R.table(["Quantity", "Formula", "Value in the project"], [
    ["Sampling rate fs", "", "48 000 Hz"],
    ["Number of samples N", "", "512 (10.67 ms of signal)"],
    ["Frequency resolution (bin width)", "Δf = fs / N", "93.75 Hz"],
    ["Highest frequency (Nyquist)", "fs / 2", "24 000 Hz (256 usable bins)"],
    ["Bin of a tone f", "k = f / Δf", "440 Hz → 4.7; 1000 Hz → 10.7; 3000 Hz → 32"],
], widths=[4.5, 3, 5], caption="FFT parameters.")
R.h3("Windowing and spectral leakage")
R.p("The DFT assumes that the 512 samples repeat periodically. A tone that does not fit an integer number of periods into the block (most tones) creates a "
    "discontinuity at the block edges and its energy **leaks** into neighbouring bins. Multiplying the block with a **window function** that tapers to zero at the edges reduces the leakage. "
    "The Hamming window used here lowers the side lobes to about -43 dB (a rectangular window: -13 dB) at the cost of a wider main lobe (about 4 bins instead of 2) "
    "and an amplitude loss (coherent gain 0.54) that has to be taken into account when absolute amplitudes are required.", align="justify")
R.figure("window_leakage", "Spectral leakage of a 1 kHz tone with a rectangular and with a Hamming window.")
R.figure("fft_demo", "Three tones (440 Hz, 1 kHz, 3 kHz) in the time domain and in the Hamming-windowed spectrum.")
R.p("**Finding tones.** The tone frequency is the bin with the largest magnitude, f ≈ k·Δf. With a resolution of 93.75 Hz the error can be up to half a bin (47 Hz); "
    "parabolic interpolation of the three bins around the peak improves it. Two tones closer than about 4 bins (375 Hz) merge into one peak. "
    "A longer block gives a finer resolution but a longer measuring time (resolution x duration ≈ 1): the time-frequency trade-off.", align="justify")

# =============================================================================
R.h1("Implementation")
R.h2("First experiment (`INMP441_spl.ino`)")
R.p("The first experiment was a single Arduino sketch: a 16 kHz I2S stream, a block of 512 samples, the RMS of the block, the level calculated as "
    "`dBSPL = dBFS + 126` (126 was found empirically: a tone of about 94 dB SPL was measured at -32 dBFS), an FFT of the block with the library arduinoFFT, and a "
    "Python script (`inmp441.py`) that received the spectrum over the serial port and plotted it. It showed that the microphone works and gave the first spectra. Its limitations led to the redesign:", align="justify")
R.bullets([
    "no frequency weighting - the number is a flat (Z-weighted) level, not dBA;",
    "16 kHz: the A-weighting filters in the reference project are designed for 48 kHz;",
    "the loop printed 255 lines of the spectrum after every block: at 115 200 baud this takes roughly half a second, while a block lasts 32 ms, so the program could not keep up and the blocks were not contiguous (no valid Leq);",
    "no tasks, no deadline control, double-precision calculations;",
    "the empirical offset of 126 dB differs from the theoretical value (see Section 4.4).",
])
R.h2("Current design")
R.h3("Tasks and data flow")
R.figure("slm_tasks", "Tasks of the sound level meter.")
R.table(["Task", "Priority / core", "Work"], [
    ["`slm_reader`", "4 / core 1", "blocks in `i2s_read` until 6000 samples (125 ms) are available; converts, filters and reduces the block to two sums of squares; puts them into a queue"],
    ["`slm_calc`", "2 / core 1", "takes the sums from the queue; calculates dB values, the 1-second Leq, flags; maintains the upload window; performs the FFT on request"],
    ["`pms_fsm` (other report)", "3 / core 0", "calls `slmTakeWindow()` at the end of every MEASURE window"],
], widths=[2.8, 2.6, 7.2], caption="Tasks of the sound level meter.")
R.p("**Why two tasks.** The reader has the **highest priority** in the whole firmware because it has a hard deadline: if it is late, the DMA buffers overflow and samples are lost. "
    "Therefore it only does the minimum (conversion, two filters, two sums). Logarithms, flags and the FFT are done by the calculation task at a lower priority. "
    "The queue (8 entries) decouples them: a short delay of the calculation task does not disturb the acquisition.", align="justify")
R.h3("Signal processing of one block")
R.numbered([
    "`i2s_read()` delivers 6000 32-bit words (125 ms at 48 kHz); the task sleeps until then.",
    "Each word is converted: the 24 valid bits are taken with an arithmetic shift of 8 bits and stored as `float` (in place, to save RAM).",
    "If an FFT was requested, the first 512 raw samples are copied (before filtering).",
    "The block passes the microphone equaliser (1 section); the sum of squares Σ x² of the equalised, unweighted signal is returned (used for overload detection).",
    "The same block continues through the A-weighting (3 sections); the sum of squares of the A-weighted signal is returned (used for LAeq).",
    "Both sums and the processing time are put into the queue; the processing time is recorded in the probe `slm_block`.",
])
R.code("""// reader task: the measured section (simplified from sound_level.cpp)
i2s_read(I2S_PORT, s_samples, SAMPLES_SHORT*sizeof(int32_t), &br, portMAX_DELAY);
uint32_t t0 = perfNowUs();
for (int i = 0; i < SAMPLES_SHORT; i++) {            // int32 (24 bit) -> float
  int32_t v;  memcpy(&v, &s_samples[i], 4);
  s_samples[i] = (float)(v >> (32 - 24));
}
q.sumSqrSPL = FILT_MIC.filter(s_samples, s_samples, SAMPLES_SHORT);   // equalizer
q.sumSqrW   = FILT_A.filter(s_samples, s_samples, SAMPLES_SHORT);     // A-weighting
q.procUs    = perfNowUs() - t0;                      // execution time C of the block
xQueueSend(s_q, &q, 0);                              // drop and count if the queue is full""")
R.h3("From sums to decibels")
R.p("With the mean square of a block m = Σx² / N and the reference amplitude derived from the datasheet sensitivity", align="justify")
R.equation("REF = 10^(−26/20) · (2²³ − 1) = 420 426 ,      REF² = 1.768 · 10¹¹", "9")
R.p("the level is", align="justify")
R.equation("L = OFFSET + 94 + 10 · log10( m / REF² )", "10")
R.p("where OFFSET = 3.0103 dB = 20·log10(√2) is the default correction for the fact that the datasheet gives the sensitivity relative to the full-scale "
    "sine wave (whose RMS value is 3 dB below its peak value). OFFSET is stored in flash and is the calibration parameter of the meter (`slm ref`, `set slmoff`).", align="justify")
R.table(["Quantity", "Computed from", "Period"], [
    ["Short level (LAeq,125 ms)", "A-weighted sum of one block", "125 ms"],
    ["Overload flag", "unweighted level of a block above 120 dB SPL", "any block of the second"],
    ["Leq (LAeq,1 s)", "A-weighted sums of 8 blocks (48 000 samples)", "1 s"],
    ["Low flag", "Leq below the noise floor (33 dBA)", "1 s"],
    ["LAmax", "largest short level in the window", "upload window (about 5 min)"],
    ["LAeq of the window", "**energy average** of the 1 s values, overloaded seconds excluded (equation 3)", "upload window"],
], widths=[3.4, 6.6, 2.6], caption="Levels calculated by the firmware.")
R.h3("Upload window")
R.p("The state machine of the sensor unit measures for 30 s and then sleeps for 240 s, but the microphone runs continuously. The calculation task therefore accumulates "
    "the energy of all valid seconds since the last upload. When the state machine finishes a MEASURE window it calls `slmTakeWindow()`, which "
    "returns LAeq and LAmax of the whole period (typically 5 minutes) and starts a new window. The values go to ThingSpeak fields 5 and 6. "
    "Access to the window is protected by a mutex because two tasks use it.", align="justify")
R.h3("Commands")
R.table(["Command", "Function"], [
    ["`slm`", "status: running, LAeq (1 s), LAeq (125 ms), flags, offset, window content"],
    ["`slm on`, `slm off`", "start / stop the microphone processing (the I2S peripheral is stopped)"],
    ["`slm fft`", "prints one spectrum in the START / END format"],
    ["`slm ref <dB>` (login)", "calibration with a reference source of known level"],
    ["`set slmoff <dB>` (login)", "set the offset directly"],
    ["`perf`", "timing of `slm_block`, `slm_calc`, `slm_fft`, CPU load, stacks"],
], widths=[4, 8.6], caption="Console commands of the sound level meter.")
R.h2("FFT on request")
R.p("An FFT is not needed for the level measurement (the IIR filters do the weighting), so it is **not run continuously**. On the command `slm fft` the reader copies 512 raw samples "
    "of the next block; the calculation task applies a Hamming window, computes the FFT with the arduinoFFT library, converts to magnitudes and prints bins 1 ... 255 as "
    "`frequency,magnitude`, enclosed by the lines `START` and `END`. This is the same format as in the first experiment, so `inmp441.py` plots it unchanged. "
    "The execution time is recorded in the probe `slm_fft`. The bin width is 93.75 Hz (the first experiment had 31.25 Hz at 16 kHz and 512 samples).", align="justify")
R.callout("Possible extension: average several FFT blocks, interpolate the peak, or detect the strongest tone automatically and send its frequency to ThingSpeak.", "note")
R.h2("Calibration")
R.p("A digital microphone is factory calibrated within about ±1 dB, but the real accuracy of a sound level meter depends on the housing, the position and the tolerance of the individual device. "
    "The calibration procedure with an **acoustic calibrator** (94 dB at 1 kHz): place the calibrator on the microphone, wait until the `slm` value is stable and type `slm ref 94`; "
    "the firmware adds the difference to OFFSET and stores it in flash. At 1 kHz the A-weighting is 0 dB, so dBA = dB SPL for this tone.", align="justify")
R.p("**Observation about the theoretical offset.** With the datasheet values the relation between the dBFS of the RMS value and the level is "
    "L = dBFS + 26 + 94 + 3.01 = **dBFS + 123.0 dB**. The first experiment found **+126 dB** empirically. The difference of 3 dB should be investigated: "
    "it may come from the reference source that was used, from the definition of dBFS (RMS or peak) or from the tolerance of the device. "
    "It is a good example of why a calibration against a reference is necessary.", align="justify")
R.callout("Verify the 3 dB difference with a calibrator or a reference meter and write down the result.", "todo")
R.h2("Real-time design")
R.figure("timing_budget", "Real-time budget: the processing C of one block must finish before the next block is complete (T = 125 ms).")
R.p("The task is periodic with the period T = 125 ms, which is also the deadline. The processor utilisation of the task is U = C/T, where C is the execution time "
    "of the filtering and conversion, measured with the probe `slm_block`. Besides C, the design has the following safety margins:", align="justify")
R.bullets([
    "**DMA buffer:** 32 banks x 375 samples x 4 bytes = 48 000 bytes = 12 000 samples = **250 ms**, i.e. two blocks of slack before samples are overwritten;",
    "**queue** of 8 sums = 1 s of results; if it is full the block is dropped and counted (`slm_block_dropped`);",
    "**priority 4** of the reader, above the state machine (3), the upload (2) and the network task (2);",
    "**separate core** (core 1) on the dual-core ESP32, so that WiFi and HTTP on core 0 do not compete for the CPU.",
])
R.table(["", "ESP32-WROOM-32", "ESP32-S2 (estimate)"], [
    ["Floating-point operations per second", "about 1.5 million (4 sections x 8 operations x 48 000)", "same"],
    ["Cost per operation", "hardware FPU, fused multiply-add; assembly routine about 10 - 12 cycles per sample and section", "software emulation, tens of cycles per operation"],
    ["Estimated cycles per second", "about 2 - 3 million (about 1 % of one core at 240 MHz)", "about 60 - 150 million (25 - 60 % of the core)"],
    ["Consequence", "a very small load", "significant load; competes with everything else on its only core"],
], widths=[3.5, 4.8, 4.3], caption="Order-of-magnitude estimate of the filter load (to be replaced by measurement).", font=8.5)
R.callout("The table is a rough estimate based on the number of operations and typical cycle counts; it is **not a measurement**. The real values are obtained with the probe `slm_block` "
          "and the CPU-load measurement (Chapter 5). The portable C++ fallback of the filter routine is used on the S2.", "check")
R.p("Further points of the RTOS design: an ESP32 task that uses the FPU is bound to one core (floating-point registers are not saved on every switch), therefore the tasks are pinned "
    "explicitly; the I2S read blocks without consuming CPU time; stacks are 4 KB and 5 KB and their high-water marks are checked with `perf`; the `perf load` command "
    "is used to find the load at which the first deadline is missed.", align="justify")
R.h2("Changes compared with the first experiment")
R.table(["Topic", "First experiment", "Current version"], [
    ["Sampling rate", "16 kHz", "48 kHz (required by the filter design)"],
    ["Block", "512 samples = 32 ms", "6000 samples = 125 ms (DMA: 32 banks x 375)"],
    ["Weighting", "none (Z)", "microphone equaliser + A-weighting"],
    ["Arithmetic", "double precision", "single precision, assembly routine on the ESP32, C++ fallback elsewhere"],
    ["Level formula", "20·log10(RMS) + empirical 126 dB", "10·log10(mean square / REF²) with a calibration offset in flash"],
    ["Structure", "everything in `loop()`", "two FreeRTOS tasks and a queue, pinned to a core"],
    ["Output", "spectrum and values printed after every block", "values in ThingSpeak; FFT only on request"],
    ["Averaging", "none", "energy average over the upload window, LAmax, overload / low flags"],
    ["Measurement", "none", "timing probes, drop counter, CPU load"],
    ["Calibration", "constant in the source", "run-time calibration stored in flash"],
], widths=[2.8, 4.2, 5.6], caption="First experiment and current version.")

# =============================================================================
R.h1("Experiments and measurements")
R.callout("This chapter defines the procedure and the result tables. The results are to be entered after the measurements; no numbers are assumed.", "todo")
R.h2("E1 - level accuracy")
R.p("Procedure: a 1 kHz tone from a tone generator (or a calibrator) is played at several levels near the microphone and compared with a reference meter (or a smartphone application with a known correction). "
    "Record the value of `slm` after it is stable.", align="justify")
R.table(["Reference level [dB]", "Measured LAeq [dBA]", "Error [dB]"], [["94 (calibrator)", "", ""], ["80", "", ""], ["70", "", ""], ["60", "", ""], ["50", "", ""]],
        widths=[4, 4, 4], caption="Level accuracy at 1 kHz.")
R.h2("E2 - frequency weighting")
R.p("Procedure: a sine sweep or a sequence of tones at constant acoustic level; the measured level is compared with the expected A-weighting of Table 1. A loudspeaker with a flat response "
    "is required; the room and the speaker influence the result, therefore compare the **difference** to the 1 kHz value.", align="justify")
R.table(["Frequency [Hz]", "125", "250", "500", "1000", "2000", "4000", "8000"], [
    ["expected relative level [dB]", "-16.1", "-8.6", "-3.2", "0", "+1.2", "+1.0", "-1.1"],
    ["measured relative level [dB]", "", "", "", "0", "", "", ""],
], widths=[3.6, 1.3, 1.3, 1.3, 1.3, 1.3, 1.3, 1.3], caption="Weighting check (expected values: IEC A-weighting).", font=8.5, align_center_cols=tuple(range(1, 8)))
R.h2("E3 - FFT and tones")
R.p("Procedure: generate pure tones of 440 Hz, 1000 Hz and 3000 Hz (and a combination), run `slm fft`, plot with `inmp441.py`. Compare the peak bin with the expected bin, estimate the error and observe the leakage.", align="justify")
R.table(["Tone [Hz]", "Expected bin", "Measured peak bin", "Estimated frequency [Hz]", "Error [Hz]"], [
    ["440", "4.7", "", "", ""], ["1000", "10.7", "", "", ""], ["3000", "32.0", "", "", ""]], widths=[2.4, 2.4, 2.8, 3, 2.2], caption="FFT test.")
R.h2("E4 - noise floor and overload")
R.bullets(["noise floor: microphone covered / quiet room - the `low` flag must appear below about 33 dBA;",
           "overload: not testable with simple means above 120 dB SPL; the flag logic can be checked by lowering `MIC_OVERLOAD_DB` temporarily."])
R.h2("E5 - execution time and processor load")
R.p("Procedure: `perf reset`, `perf cpu on`, wait 60 s, `perf`. Repeat for the three configurations of the first report (A: without sound, B: two cores, C: one core) "
    "and for increasing artificial load (`perf load`).", align="justify")
R.table(["Quantity", "Config B (2 cores)", "Config C (1 core)"], [
    ["`slm_block` avg / max [µs]", "", ""],
    ["`slm_block` budget 125 000 µs - overruns", "", ""],
    ["`slm_calc` avg / max [µs]", "", ""],
    ["`slm_fft` [µs]", "", ""],
    ["`slm_block_dropped`", "", ""],
    ["CPU load average / peak [%]", "", ""],
    ["Load at which the first overrun appears [%]", "", ""],
    ["free stack `slm_reader` / `slm_calc` [bytes]", "", ""],
], widths=[6, 3.3, 3.3], caption="Timing results (to be measured).", font=8.5)
R.h2("E6 - long run")
R.p("A 24-hour run: LAeq values in ThingSpeak, the minimum free heap, the number of drops and the number of restarts.", align="justify")

# =============================================================================
R.h1("Discussion")
R.bullets([
    "**Instrument class.** A MEMS microphone with an unknown housing is not a Class 1/2 sound level meter; the result is an indicative measurement with an uncertainty of a few dB, and the calibration quality is decisive.",
    "**Range.** About 33 - 120 dBA; below the noise floor the value is flagged, above the acoustic overload point it is excluded.",
    "**Time weighting.** The short level is a 125 ms linear average, not an exponential Fast / Slow detector; LAmax is therefore the maximum 125 ms level.",
    "**Frequency weighting accuracy.** The IIR A-weighting follows the analytic curve within 0.3 dB (20 Hz - 16 kHz); the microphone equaliser depends on a datasheet curve that was read visually.",
    "**Computation.** The estimate suggests a negligible load on the ESP32 and a significant one on the ESP32-S2 (no FPU). The measurement must confirm this.",
    "**FFT.** Resolution 93.75 Hz is sufficient to recognise tones and the character of noise, not to separate close tones.",
])

# =============================================================================
R.h1("Conclusions and further work")
R.p("A digital sound level meter was added to the sensor unit. The signal path (I2S, DMA, equaliser, A-weighting, energy accumulation) is based on a proven open-source design, "
    "the coefficients were checked against the analytic A-weighting, the processing is organised in prioritised FreeRTOS tasks with measurable timing, and the FFT is available on request. "
    "The level is calibrated with a stored offset.", align="justify")
R.p("**Further work:** the measurements of Chapter 5, repeating the coefficient design with own scripts, C-weighting and peak detection, 1/3-octave band analysis, "
    "Fast / Slow time weighting, automatic tone detection, and the optimisation of the critical code in the subject Optimisation in C "
    "(fixed-point biquads, the optimised DSP routines of the ESP-DSP library, loop unrolling, using the processor cycle counter for measurements).", align="justify")

# =============================================================================
R.h1("Appendices", numbered=False)
R.h2("A. Filter coefficients (from `sound_level.cpp`)")
R.p("Convention: section = {b1, b2, a1, a2}, b0 = 1, the stored a1 and a2 are the negatives of the classical denominator coefficients.", align="justify")
R.table(["Filter", "Gain", "Section", "b1", "b2", "a1", "a2"], [
    ["INMP441 equaliser", "1.00197834654696", "1", "-1.986920458344451", "+0.986963226946616", "+1.995178510504166", "-0.995184322194091"],
    ["A-weighting", "0.169994948147430", "1", "-2.00026996133106", "+1.00027056142719", "-1.060868438509278", "-0.163987445885926"],
    ["", "", "2", "+4.35912384203144", "+3.09120265783884", "+1.208419926363593", "-0.273166998428332"],
    ["", "", "3", "-0.70930303489759", "-0.29071868393580", "+1.982242159753048", "-0.982298594928989"],
], widths=[2.4, 2.4, 1, 2.2, 2.2, 2.2, 2.2], caption="Second-order sections used in the firmware.", font=7.5)
R.h2("B. Main constants")
R.table(["Constant", "Value", "Meaning"], [
    ["`SLM_SAMPLE_RATE`", "48 000 Hz", "fixed by the filter design"],
    ["`SAMPLES_SHORT`", "6000", "samples per block (125 ms)"],
    ["`DMA_BANKS` x `DMA_BANK_SIZE`", "32 x 375", "DMA ring = 250 ms"],
    ["`LEQ_PERIOD_S`", "1 s", "Leq period"],
    ["`MIC_SENSITIVITY`, `MIC_REF_DB`", "-26 dBFS, 94 dB", "datasheet reference"],
    ["`MIC_OVERLOAD_DB`, `MIC_NOISE_DB`", "120 dB, 33 dB", "valid range"],
    ["`FFT_N`", "512", "FFT length"],
    ["Pins", "SCK 32, WS 5, SD 33", "ESP32-WROOM-32 (adapt for the S2 mini)"],
    ["Priorities", "reader 4, calc 2", "see `app_config.h`"],
], widths=[5, 3.5, 4.2], caption="Constants of the sound level meter.")
R.h2("C. Own derivation of the A-weighting (exercise)")
R.p("The comparison in Section 2.8 can be reproduced with NumPy (no additional packages): evaluate the analogue A-weighting, equation 4, and the bilinear mapping "
    "and compare them with the response of the sections of Appendix A:", align="justify")
R.code("""import numpy as np
fs = 48000.0
f1, f2, f3, f4 = 20.598997, 107.65265, 737.86223, 12194.217
w = lambda f: 2*np.pi*f
def Hbil(f):
    z = np.exp(1j*2*np.pi*f/fs);  s = 2*fs*(z-1)/(z+1)
    return s**4 / ((s+w(f1))**2 * (s+w(f2)) * (s+w(f3)) * (s+w(f4))**2)
f = np.array([100, 1000, 8000, 16000.0])
print(20*np.log10(abs(Hbil(f)) / abs(Hbil(np.array([1000.0]))[0])))   # normalised to 0 dB at 1 kHz""")
R.h2("D. References")
R.bullets([
    "I. Kostoski, esp32-i2s-slm - Sound Level Meter with ESP32 and I2S MEMS microphone, https://github.com/ikostoski/esp32-i2s-slm (GPL-3.0); project page on hackaday.io.",
    "TDK InvenSense, INMP441 datasheet (omnidirectional microphone with bottom port and I2S digital output).",
    "IEC 61672-1, Electroacoustics - Sound level meters - Part 1: Specifications.",
    "Matt L., A-weighting IIR filter design, https://dsp.stackexchange.com/a/36122 (source of the coefficients in the reference project).",
    "Espressif, ESP-IDF Programming Guide: I2S driver, FreeRTOS (IDF), https://docs.espressif.com/projects/esp-idf/",
    "Espressif ESP-DSP library (optimised DSP routines for ESP32) - for the optimisation in C.",
    "A. V. Oppenheim, R. W. Schafer, Discrete-Time Signal Processing, Pearson.",
    "arduinoFFT library, https://github.com/kosme/arduinoFFT",
    "Philips Semiconductors, I2S bus specification (1986, revised 1996).",
])
R.callout("Additional literature, links and diagrams provided by the author are to be added here.", "todo")

R.save(OUT)
