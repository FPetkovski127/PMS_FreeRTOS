"""Builds Report 1: PMS5003T sensor unit + LED matrix display unit, FreeRTOS implementation (draft)."""
import os
from docx_helpers import Report

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "PMS_Matrix_RTOS_Report_DRAFT_v0.2.docx")

R = Report("Air-quality monitoring with FreeRTOS - PMS5003T sensor unit and LED matrix display unit (draft)")

# =============================================================================
R.title_page(
    "Air-Quality Monitoring System\nwith FreeRTOS",
    "PMS5003T sensor unit and LED matrix display unit on ESP32",
    ["Technical documentation - DRAFT v0.2",
     "Subject: Real-Time Operating Systems",
     "Author: [name, index number]",
     "Mentor: [name]",
     "Date: 16.11.2026"],
    abstract="This report describes a school air-quality system. A sensor unit measures particulate matter (PM2.5, PM10), "
             "temperature and humidity with a PMS5003T sensor and uploads the values to ThingSpeak. A second unit reads "
             "the values from ThingSpeak and shows them on a chain of WS2812B LED matrices. The first generation ran "
             "as a super-loop; this version is built on FreeRTOS. The report gives the theory (sensor, microcontrollers, "
             "LED matrices, real-time operating systems), the design of the new firmware, what was changed compared with the "
             "earlier versions, and a test and performance-measurement plan. The sound level meter is described in a separate report.")
R.toc()

# =============================================================================
R.h1("Introduction")
R.h2("Purpose and scope")
R.p("Air quality in classrooms and school buildings influences concentration and health. The project is a small "
    "monitoring network: one or more **sensor units** measure the air and publish the results, and **matrix display units** "
    "show the current values to students and teachers as scrolling, colour-coded text. The two kinds of units "
    "communicate through the ThingSpeak cloud service, so they need only a WiFi connection and no direct link to each other.")
R.p("This report covers the PMS5003T sensor unit and the LED matrix unit, and the way their firmware was re-designed on top of "
    "the FreeRTOS real-time operating system. The additional sound level meter (INMP441 microphone) is part of the sensor "
    "firmware but is documented in a separate report, because it is mostly a digital signal processing topic.", align="justify")
R.figure("system_overview", "System overview: sensor unit, ThingSpeak channels and matrix display unit.")
R.h2("Goals of the new version")
R.bullets([
    "Replace the super-loop with **FreeRTOS tasks** so that slow operations (network) cannot delay time-critical ones (sensor reading, display scrolling).",
    "A sensor cycle with a **finite state machine**: 30 s warm-up, 30 s measuring, 240 s sleep (normal mode); fast mode without sleep for testing.",
    "**Reliable WiFi**: automatic recovery, a fallback (service) network for firmware updates, and a protected configuration portal.",
    "**Safe configuration storage** that survives resets and power loss, with an access code against unauthorised changes.",
    "**Calibration coefficients** that can be changed without a new firmware (stored in flash, updated remotely through ThingSpeak).",
    "**Measurable real-time behaviour**: timing of critical sections, CPU load, stack and heap usage.",
    "A clear, modular code structure that can be explained and extended (sound level meter, new displays).",
])
R.h2("Structure of the report")
R.p("Chapter 2 gives the theoretical background. Chapter 3 describes the hardware. Chapter 4 describes the software architecture "
    "and every functional block. Chapter 5 lists what was changed compared with the earlier versions of the project. "
    "Chapter 6 contains the test procedure and Chapter 7 the performance analysis. Chapter 8 gives conclusions and further work.")

# =============================================================================
R.h1("Theoretical background")
R.h2("Particulate matter and the PMS5003T sensor")
R.h3("Particulate matter")
R.p("Particulate matter (PM) is a mixture of solid particles and liquid droplets suspended in air. It is classified by the "
    "aerodynamic diameter of the particles: **PM10** are particles up to 10 µm, **PM2.5** up to 2.5 µm and **PM1.0** up to 1.0 µm. "
    "The result is reported as a mass concentration in µg/m³. Fine particles (PM2.5) penetrate deep into the respiratory "
    "system, therefore guidelines (WHO, EU, US EPA) define limit values for PM2.5 and PM10. The colour thresholds used by "
    "the display unit are given in Section 4.11.", align="justify")
R.h3("Measuring principle")
R.p("The PMS5003T is a digital laser-scattering particle sensor. A fan draws air through a chamber that is crossed by a "
    "laser beam. Particles scatter the light; a photodiode converts the scattered light pulses into electrical signals and an "
    "integrated microcontroller evaluates pulse height and number with a Mie-scattering based algorithm. The sensor outputs "
    "the particle counts per 0.1 litre of air for several size classes and the calculated mass concentrations. The “T” version "
    "additionally contains a temperature and a relative-humidity sensor.", align="justify")
R.table(["Property", "Typical value (manufacturer datasheet)"], [
    ["Measured sizes", "0.3 - 1.0 µm, 1.0 - 2.5 µm, 2.5 - 10 µm (counts); PM1.0, PM2.5, PM10 (mass)"],
    ["Supply voltage", "5 V (4.5 - 5.5 V)"],
    ["Logic level (RX/TX/SET)", "3.3 V - directly compatible with the ESP32"],
    ["Current", "about 100 mA active (fan on), below 0.2 mA in sleep"],
    ["Interface", "UART 9600 baud, 8N1, 32-byte frames; active or passive mode"],
    ["Stabilisation after wake-up", "at least 30 s before the data are reliable"],
    ["Resolution", "1 µg/m³"],
    ["Temperature / humidity", "from the on-board sensors, 0.1 °C / 0.1 % RH resolution in the frame"],
], widths=[3, 7], caption="Main properties of the PMS5003T.")
R.callout("The values above are typical manufacturer values. Check the datasheet revision of the sensor actually used "
          "(accuracy, range and response time are not repeated here).", "check")
R.h3("Data frame")
R.p("In active mode the sensor sends a 32-byte frame about once per second. The firmware parser (file `pms5003t.cpp`) "
    "synchronises on the two start bytes, collects 32 bytes, verifies the length field and the checksum and then extracts the values.")
R.table(["Bytes", "Content", "Used"], [
    ["0 - 1", "Start bytes 0x42 0x4D", "synchronisation"],
    ["2 - 3", "Frame length (28)", "validation"],
    ["4 - 9", "PM1.0 / PM2.5 / PM10, CF=1 (factory calibration)", "optional (`PMS_USE_ATMOSPHERIC 0`)"],
    ["10 - 15", "PM1.0 / PM2.5 / PM10, atmospheric environment", "**PM2.5 and PM10 (default)**"],
    ["16 - 23", "Particle counts 0.3 / 0.5 / 1.0 / 2.5 µm", "not used"],
    ["24 - 25", "Temperature, signed, 0.1 °C", "**used**"],
    ["26 - 27", "Relative humidity, 0.1 %", "**used**"],
    ["28 - 29", "Reserved", "-"],
    ["30 - 31", "Checksum = sum of bytes 0 ... 29", "validation"],
], widths=[1.5, 6, 4], caption="PMS5003T frame layout as implemented in the parser.")
R.callout("The offsets and the choice between CF=1 and atmospheric values were taken from the datasheet layout. "
          "They must be verified with a known reading and with the original `PMS5003T.h` of the earlier versions.", "check")
R.h3("Sleep control and why a state machine is needed")
R.p("The SET pin controls the fan: high = normal operation, low = sleep. Running the fan continuously shortens the life of the sensor, "
    "so the school installations measure for a short time and sleep in between. After waking up the sensor needs about 30 s "
    "to stabilise, therefore the cycle is WARMUP (discard data), MEASURE (collect and average) and SLEEP (fan off). "
    "This is a classic finite state machine (Section 4.4).", align="justify")
R.h3("Temperature and humidity readings")
R.p("The temperature sensor is located inside the housing next to the fan and electronics, so it can read a little higher than the "
    "ambient temperature. A linear correction (gain and offset) is therefore provided for every measured quantity (Section 4.6).")

R.h2("ESP32-WROOM-32 / NodeMCU-32S (development platform)")
R.p("The prototype uses an ESP32 development board with the ESP32-WROOM-32 module and a CH340 USB-to-serial converter. "
    "It is used for development because it has two cores and a floating-point unit, which makes timing experiments easier.", align="justify")
R.table(["Property", "ESP32-WROOM-32 (NodeMCU-32S)"], [
    ["CPU", "Xtensa LX6, dual core, up to 240 MHz"],
    ["Memory", "520 KB SRAM, 4 MB flash on the module"],
    ["Floating point", "hardware FPU, single precision"],
    ["Wireless", "WiFi 802.11 b/g/n (2.4 GHz), Bluetooth classic + BLE"],
    ["Peripherals (used here)", "UART2 (PMS), I2C (OLED), I2S (microphone), GPIO (SET pin, LED, LED matrix data)"],
    ["Logic voltage", "3.3 V"],
    ["USB", "external USB-UART converter (CH340) - serial monitor and programming"],
], widths=[3, 7], caption="ESP32-WROOM-32 properties relevant for the project.")
R.h2("ESP32-S2 mini (target platform for the schools)")
R.p("The units installed in schools will use the ESP32-S2 mini. It is smaller and cheaper, but it differs in ways that matter for a "
    "real-time design.", align="justify")
R.table(["Property", "ESP32-WROOM-32", "ESP32-S2 mini", "Consequence for this project"], [
    ["CPU cores", "2 (LX6)", "**1** (LX7)", "all tasks share one core; priorities become the only protection against delays"],
    ["FPU", "yes", "**no**", "float maths is emulated in software and is much slower (important for the sound level meter)"],
    ["SRAM", "520 KB", "320 KB", "buffers (DMA, FFT) must be sized with care"],
    ["Wireless", "WiFi + Bluetooth", "WiFi only", "no impact (Bluetooth is not used)"],
    ["USB", "external converter", "native USB", "the serial console runs over USB CDC; different reset behaviour"],
    ["I2S ports", "2", "1", "enough for one microphone"],
    ["GPIO / pinout", "many", "fewer, different numbers", "pin definitions are in `app_config.h` and must be adapted"],
], widths=[2.2, 2.4, 2.4, 6], caption="Differences between the development board and the target board.")
R.callout("The firmware is written so that it builds for both chips: `i2Core()` maps every task to core 0 on a single-core chip, "
          "the filter code has a portable fallback for chips without the Xtensa FPU assembly, and the switch "
          "`TEST_SINGLE_CORE` puts all tasks on one core of the ESP32 to imitate the S2 during measurements.", "note")
R.callout("Exact memory sizes and pin availability depend on the board variant; verify them for the board that is purchased.", "check")

R.h2("Addressable LED matrix (WS2812B / NeoPixel)")
R.p("The display uses 16 x 8 panels with WS2812B LEDs (also sold as NeoPixel). Each LED contains a red, a green and a blue emitter and a small "
    "controller chip. The LEDs are connected in a chain: the first LED receives the data on its DIN pin, takes its own 24 bits and "
    "forwards the rest from DOUT to the next LED. A single data wire is enough.", align="justify")
R.bullets([
    "**Protocol:** 800 kHz, 24 bits per LED in G-R-B order. A bit is a pulse of about 0.4 µs high (0) or 0.8 µs high (1) within a 1.25 µs period; a low time above about 50 µs latches the data.",
    "**Update time:** 24 bits x 1.25 µs = 30 µs per LED; for 512 LEDs (64 x 8) about 15.4 ms per frame, i.e. at most 65 frames per second. The scroll step of 60 ms therefore leaves enough margin.",
    "**Current:** up to about 60 mA per LED at full white. A 64 x 8 panel could draw about 30 A at full brightness; the firmware limits the brightness to 20/255 (about 8 %), which gives a few amperes at most.",
    "**Power:** a 5 V supply is injected at every panel, grounds are common with the ESP32. A level shifter (3.3 V to 5 V, for example 74HCT125) and a 330 Ω series resistor in the data line are recommended.",
    "**Layout:** the panels use zig-zag column wiring (every second column is reversed). The library `Adafruit_NeoMatrix` hides this with the flags `NEO_MATRIX_TOP + NEO_MATRIX_LEFT + NEO_MATRIX_COLUMNS + NEO_MATRIX_ZIGZAG`.",
])
R.h2("ThingSpeak")
R.p("ThingSpeak is an IoT cloud service with channels that have up to eight numeric fields. A device writes with a **write API key** and "
    "reads with a **read API key** (or without a key from a public channel) using simple HTTP requests. The free licence limits the update "
    "interval to 15 s per channel and the total number of messages per year; the firmware keeps a minimum gap of 16 s between writes.", align="justify")
R.table(["Field", "Content", "Written by", "Used by"], [
    ["1", "PM2.5 [µg/m³]", "sensor unit", "matrix unit"],
    ["2", "PM10 [µg/m³]", "sensor unit", "matrix unit"],
    ["3", "Temperature [°C]", "sensor unit", "matrix unit"],
    ["4", "Relative humidity [%]", "sensor unit", "matrix unit"],
    ["5", "LAeq [dBA] (optional, microphone)", "sensor unit", "matrix unit (optional)"],
    ["6", "LAmax [dBA] (optional)", "sensor unit", "-"],
], widths=[1.2, 5, 3, 3.5], caption="Data channel layout.")

R.h2("Real-time operating systems and FreeRTOS")
R.h3("What is a real-time system")
R.p("In a real-time system the **correctness of a result depends on the time at which it is produced**, not only on its value. "
    "A *hard* real-time system must never miss a deadline (an airbag controller); in a *soft* real-time system an occasional "
    "late result only degrades quality (a video player). Real-time does not mean fast: it means **predictable**. "
    "In this project the I2S audio block must be processed every 125 ms (hard-like), the state machine must poll the sensor "
    "about every 50 ms, and the display must scroll smoothly (soft).", align="justify")
R.h3("Super-loop versus RTOS")
R.p("A super-loop executes all actions one after another in one endless `loop()`. It is simple, but the response time to any event "
    "is the sum of the duration of everything else in the loop, and one blocking call (for example an HTTP request that waits for the "
    "network) delays everything. An RTOS splits the work into independent **tasks**; a **scheduler** decides which task runs and "
    "tasks that wait for something (time, data, a lock) do not use the processor.", align="justify")
R.figure("superloop_vs_rtos", "The same work as a super-loop and as RTOS tasks.")
R.table(["Aspect", "Super-loop", "RTOS (FreeRTOS)"], [
    ["Structure", "one loop, global state, flags", "independent tasks with own stacks"],
    ["Response time", "worst case = whole loop duration", "determined by priority, not by other work"],
    ["Blocking operations", "block everything", "block only the calling task"],
    ["Timing", "delay() / millis() polling", "delays, timeouts and events handled by the kernel"],
    ["Cost", "no overhead", "memory per task (stack), context-switch time, more concepts"],
    ["Extending the system", "every new function lengthens the loop", "add a task, choose its priority"],
    ["Typical risks", "missed events, jitter", "race conditions, deadlock, priority inversion"],
], widths=[2.5, 4.5, 5], caption="Comparison of the two execution models.")
R.h3("Tasks and the scheduler")
R.p("A **task** is an endless function with its own stack and priority. At any moment a task is in one of the states shown below. "
    "FreeRTOS uses a **fixed-priority preemptive scheduler**: the highest-priority task that is READY always runs; a higher-priority task "
    "that becomes ready preempts the running one immediately. Tasks of equal priority share the processor in time slices "
    "(round robin) at every **tick** (1 ms on the ESP32 Arduino build). When no task is ready the **idle task** runs. "
    "The idea of giving the highest priority to the most frequent/most urgent activity is called rate-monotonic assignment.", align="justify")
R.figure("task_states", "Task states and transitions in FreeRTOS.")
R.table(["Term", "Meaning"], [
    ["Priority", "integer; higher number = more urgent (0 is the idle task). Fixed unless changed by the program"],
    ["Stack", "private memory of the task for local variables and calls. On the ESP32 the size is given in **bytes**. Too small = overflow = crash"],
    ["Tick", "periodic timer interrupt that drives delays, timeouts and time slicing"],
    ["Preemption", "the kernel takes the CPU from a running task because a more urgent task became ready"],
    ["Context switch", "saving the registers of one task and restoring those of another"],
    ["Idle task", "lowest priority; runs when nothing else is ready; also frees memory of deleted tasks and feeds watchdogs"],
    ["Critical section", "short code region executed with interrupts (or the other core) locked out"],
], widths=[2.5, 9.5], caption="Basic RTOS vocabulary.")
R.h3("Communication and synchronisation")
R.p("Tasks must exchange data and coordinate access to shared resources. Reading and writing the same variable from two tasks "
    "without protection is a **race condition**. FreeRTOS offers the following mechanisms.", align="justify")
R.h3("Queues")
R.p("A queue is a thread-safe FIFO buffer. The sender copies an item into the queue (`xQueueSend`), the receiver removes it "
    "(`xQueueReceive`). A task that waits on an empty queue is blocked and uses no CPU; the same holds for a sender when the "
    "queue is full (or the sender can use a zero timeout and drop the item, which the firmware does and counts). "
    "Queues decouple producer and consumer so that they can run at different speeds.", align="justify")
R.figure("queue", "A queue connects a producer task and a consumer task.")
R.h3("Semaphores and mutexes")
R.p("A **semaphore** is a counter that tasks can take and give. A *binary* semaphore (0 or 1) is used for signalling, for example "
    "an interrupt tells a task that data are ready. A *counting* semaphore counts events or manages a pool of identical resources. "
    "A **mutex** (mutual exclusion) is a special binary semaphore used to protect a shared resource: the task that takes it owns it "
    "and must give it back; only the owner may release it. While one task holds the mutex, other tasks that want it are blocked, "
    "so only one task at a time works with the data.", align="justify")
R.table(["Object", "Purpose", "Owner", "Priority inheritance", "Example in this project"], [
    ["Binary semaphore", "signalling between tasks / ISR", "none", "no", "(not needed)"],
    ["Counting semaphore", "count events, limit resource users", "none", "no", "(not needed)"],
    ["Mutex", "exclusive access to shared data", "task that took it", "**yes**", "latest sample, settings in flash, sound window"],
    ["Queue", "pass data with buffering", "-", "-", "averaged result to the upload task; I2S sums to the calc task"],
    ["Task notification", "light, fast signal to one task", "-", "-", "wake the state machine when the mode changes"],
], widths=[2.4, 3.4, 2, 2.2, 4], caption="Synchronisation objects and where they are used.")
R.h3("Priority inversion")
R.p("If a low-priority task holds a mutex that a high-priority task needs, the high-priority task has to wait. If a medium-priority task then "
    "runs, it delays the low-priority task and indirectly the high-priority one - this is **priority inversion**. FreeRTOS mutexes "
    "use **priority inheritance**: the owner temporarily gets the priority of the waiting task, so it finishes quickly and releases the mutex.", align="justify")
R.figure("priority_inversion", "Priority inversion and priority inheritance.")
R.h3("Other tools")
R.bullets([
    "**Task notifications** - each task has a built-in notification value; faster and lighter than a semaphore. The state machine waits on its notification with a 50 ms timeout: the wait is its loop delay and the wake-up when the user changes the mode.",
    "**Event groups** and **software timers** exist but are not used here.",
    "**Critical sections** (`portENTER_CRITICAL`) protect very short operations such as updating the timing statistics. They must be as short as possible.",
    "**Watchdogs** - the ESP32 has built-in watchdog timers (Section 4.8): the task watchdog resets the chip if a monitored task does not report in time. No external parts are used.",
])
R.h3("Typical problems")
R.table(["Problem", "Cause", "Counter-measure used"], [
    ["Race condition", "two tasks modify the same data", "mutexes, queues, single-writer variables"],
    ["Deadlock", "two tasks wait for each other's mutex", "never hold two mutexes; short critical sections"],
    ["Priority inversion", "low task holds a lock that a high task needs", "mutex with priority inheritance"],
    ["Starvation", "a high-priority task never blocks", "every task waits in a delay or on a queue; load generator limited to 95 %"],
    ["Stack overflow", "task stack too small", "stack high-water mark is measured (`perf` command)"],
    ["Queue overflow", "consumer slower than producer", "drop counters, measured timing"],
], widths=[2.6, 4.4, 5], caption="Typical RTOS problems and the counter-measures.")
R.h3("FreeRTOS on the ESP32")
R.p("The ESP32 Arduino core runs on ESP-IDF, which contains a FreeRTOS port that supports two cores (symmetric multiprocessing). "
    "Tasks can be pinned to a core (`xTaskCreatePinnedToCore`) or left free. The Arduino `loop()` is itself a task. The WiFi and TCP/IP "
    "stack run in system tasks, mostly on core 0, so application tasks that must not be disturbed are placed on core 1. "
    "On the single-core ESP32-S2 all tasks share one core.", align="justify")
R.table(["FreeRTOS function", "Use in the project"], [
    ["`xTaskCreatePinnedToCore`", "creation of all tasks (wrapped by `i2CreateTask`)"],
    ["`vTaskDelay`, `ulTaskNotifyTake`", "periodic work and event-driven waiting"],
    ["`xQueueCreate`, `xQueueSend`, `xQueueReceive`, `xQueueOverwrite`", "data between tasks (the matrix unit keeps only the newest reading with `xQueueOverwrite`)"],
    ["`xSemaphoreCreateMutex`, `xSemaphoreTake`, `xSemaphoreGive`", "protect settings, latest sample, sound window"],
    ["`xTaskNotifyGive`", "wake the state machine after a settings change"],
    ["`i2WdtInit`, `i2WdtAddSelf`, `i2WdtFeed` (task watchdog)", "internal watchdog of the state machine and of the display task"],
    ["`uxTaskGetStackHighWaterMark`, `eTaskGetState`", "stack and state in the `perf` report"],
    ["idle hook (`esp_register_freertos_idle_hook_for_cpu`)", "CPU load measurement"],
], widths=[5.5, 6.5], caption="FreeRTOS API functions used in the firmware.")
R.h3("Real-time analysis")
R.p("For periodic tasks with period T and worst-case execution time C the **processor utilisation** is", align="justify")
R.equation("U = Σ ( C_i / T_i )", "1")
R.p("For fixed-priority (rate-monotonic) scheduling a sufficient schedulability condition is the Liu and Layland bound", align="justify")
R.equation("U ≤ n · ( 2^(1/n) − 1 )", "2")
R.p("which is 0.78 for n = 3 and approaches 0.69 for many tasks. The worst-case response time of a task includes its own execution "
    "time plus the interference of all higher-priority tasks. In this project C is not guessed but **measured** (microsecond probes, "
    "Chapter 7) and compared with the period or deadline.", align="justify")

# =============================================================================
R.h1("Hardware")
R.h2("Sensor unit")
R.p("The sensor unit consists of the ESP32, the PMS5003T, an optional OLED display for the bench (the matrix unit is the final display), "
    "the optional INMP441 microphone. The ESP32 internal watchdogs are used for recovery; no external watchdog circuit is needed.", align="justify")
R.figure("sensor_hw", "Connection of the sensor unit (ESP32-WROOM-32 pin numbers).")
R.table(["Signal", "ESP32 pin", "Remark"], [
    ["PMS5003T TX", "GPIO16 (RX2)", "UART 9600 baud"],
    ["PMS5003T RX", "GPIO17 (TX2)", "not used for data in active mode"],
    ["PMS5003T SET", "GPIO4", "high = fan on, low = sleep"],
    ["OLED SDA / SCL", "GPIO21 / GPIO22", "I2C, address 0x3C"],
    ["INMP441 SCK / WS / SD", "GPIO32 / GPIO5 / GPIO33", "I2S, L/R pin to GND"],
    ["Status LED", "GPIO2", "on-board LED"],
], widths=[4, 4, 4], caption="Pin assignment of the sensor unit.")
R.callout("The pin numbers are valid for the ESP32-WROOM-32 board. The ESP32-S2 mini has a different pinout and the definitions in `app_config.h` must be adapted.", "warn")
R.h2("Matrix display unit")
R.figure("matrix_hw", "Matrix display unit: four 16 x 8 panels in a chain (64 x 8).")
R.p("The ESP32 sends the pixel data on a single pin (GPIO5 in the current code) to the DIN input of the first panel. "
    "The panels are chained DOUT to DIN. The 16 x 8 test panel and the 64 x 8 final display use the same code; "
    "only `MATRIX_W` in `app_config.h` changes.", align="justify")

# =============================================================================
R.h1("Software architecture")
R.h2("Layers and files")
R.figure("layers", "Software layers: sketches, shared library, Arduino-ESP32 with FreeRTOS.")
R.table(["Part", "File(s)", "Responsibility"], [
    ["Shared library", "`I2Settings`", "settings in flash, validation, calibration helpers"],
    ["", "`I2Wifi`", "connection supervisor, service network, config portal, OTA"],
    ["", "`I2Console`", "serial command line, setup wizard, access code"],
    ["", "`I2Perf`", "timing probes, CPU load, stack/heap report, load generator"],
    ["Sensor sketch", "`app_config.h`", "all constants: pins, timings, priorities, feature switches"],
    ["", "`app_state.*`", "shared data, probes"],
    ["", "`pms5003t.*`", "frame parser"],
    ["", "`task_pms.cpp`", "state machine"],
    ["", "`task_thingspeak.cpp`", "upload, retries, remote calibration"],
    ["", "`task_display.cpp`, `task_misc.cpp`", "OLED, status LED"],
    ["", "`sensor_console.cpp`", "commands `perf`, `slm`, `cal`"],
    ["", "`sound_level.*`, `sos-iir-filter.h`", "sound level meter (separate report)"],
    ["Matrix sketch", "`Matrix_Display_RTOS.ino`, `app_config.h`", "ThingSpeak read task, display task"],
], widths=[2.4, 4.6, 5.6], caption="Files and responsibilities.")
R.h2("Tasks, queues and shared data (sensor unit)")
R.figure("sensor_tasks", "Sensor unit: tasks with priority and core, queues and mutex-protected data.")
R.table(["Task", "Prio", "Core", "Period / trigger", "Job"], [
    ["`slm_reader`", "4", "1", "every 125 ms (I2S block)", "read microphone block, filter (separate report)"],
    ["`pms_fsm`", "3", "0", "50 ms", "state machine, UART parsing, calibration, averaging; feeds the task watchdog"],
    ["`thingspeak`", "2", "0", "queue, 1 s poll", "upload with retries, calibration check"],
    ["`slm_calc`", "2", "1", "per block", "dB, Leq, FFT (separate report)"],
    ["`wifiTask`", "2", "0", "100 ms", "connection supervisor, OTA, portal"],
    ["`display`, `led`", "1", "0", "250 ms / 250-500 ms", "OLED, status LED"],
    ["`console`", "1", "0", "50 ms", "serial commands"],
], widths=[2.4, 1, 1, 3.3, 5.3], caption="Task table of the sensor unit (core 0 / 1 apply to the dual-core ESP32).", align_center_cols=(1, 2))
R.p("**Design rules.** (1) The most time-critical work has the highest priority and, on a dual-core chip, its own core. "
    "(2) The task with a blocking network call is below the state machine. (3) Tasks do not call each other; they exchange data only "
    "through queues, mutex-protected variables and notifications. (4) No task polls with `delay()` for something that can be signalled.", align="justify")
R.h2("Start-up sequence")
R.numbered([
    "Initialise serial and pins (SET low, LED), start the internal task watchdog (`i2WdtInit`).",
    "`i2cfgBegin`: load the settings from flash or write factory defaults on the first boot.",
    "`appStateInit`: create queue, mutex, register timing probes.",
    "`perfBegin`: measure the idle rate of the CPU (about 300 ms) - this is the 0 % load reference.",
    "`i2wifiBegin`: start the connection supervisor.",
    "Create the tasks (state machine, upload, LED, display), start the sound level meter and the console.",
    "`loop()` suspends itself forever; all work is done by tasks.",
])
R.h2("PMS state machine")
R.figure("fsm", "State machine of the sensor cycle.")
R.table(["State", "Duration", "Actions"], [
    ["WARMUP", "30 s", "SET = high (fan on); frames are read and discarded"],
    ["MEASURE", "30 s (fast mode: 60 s)", "each valid frame is calibrated and added to the sums; at the end the average is computed, the sound window is added and the result is put into the queue"],
    ["SLEEP", "240 s (configurable 10 - 3600 s)", "SET = low (fan off); waits on a notification with timeout"],
], widths=[2.2, 3.8, 7], caption="States of the sensor cycle.")
R.p("**Timing calculation (normal mode).** One cycle takes 30 s + 30 s + 240 s = 300 s, i.e. one ThingSpeak update every 5 minutes (288 per day). "
    "The fan runs 60 s of 300 s, a duty cycle of 20 %. With a current of about 100 mA while the fan is on and a negligible standby current "
    "the average sensor current is about 20 mA. With about one frame per second a MEASURE window yields roughly 30 samples that are averaged "
    "to reduce noise.", align="justify")
R.p("**Fast mode** skips the SLEEP state: after the first warm-up the sensor stays on and MEASURE repeats every 60 s (1440 updates per day). "
    "It is used for tests and demonstrations; school installations run in normal mode. Because the mode is stored in flash, it must be set back "
    "to normal before a unit is installed.", align="justify")
R.p("**Event-driven wake-up.** In every loop the task waits on its notification with a 50 ms timeout. This is at the same time the loop delay and the "
    "way to interrupt a long sleep: a mode change sends a notification and the state machine reacts within one loop instead of after minutes.", align="justify")
R.h2("ThingSpeak task")
R.p("The state machine never touches the network. At the end of a MEASURE window it puts one message (average values and, if available, the sound "
    "values) into a queue of length 4. The `thingspeak` task takes the message, waits until WiFi is connected and at least 16 s have passed since the last write, "
    "and uploads it. A failed upload is retried up to three times, 20 s apart; after that the average is dropped and the failure is counted. "
    "A blocking HTTP request therefore cannot delay the sensor polling.", align="justify")
R.h2("Calibration")
R.p("Each quantity (PM2.5, PM10, temperature, humidity) is corrected with a straight line:", align="justify")
R.equation("corrected = gain · raw + offset", "3")
R.p("The values are stored in flash and applied to **every sample**, so the display and the average already show corrected values "
    "(an average of corrected values equals the corrected average because the correction is linear). After a sensor has been replaced the "
    "coefficients can be changed without a new firmware: with the serial console, or remotely through an optional second ThingSpeak channel whose field 1 "
    "contains `version,g25,o25,g10,o10,gT,oT,gH,oH`. The unit checks it every hour. A newer version is only **offered** to the operator "
    "(`cal`, `cal accept`, `cal reject`) unless automatic acceptance (`cal auto on`) is enabled. Values outside the range "
    "gain 0.1 - 10 and offset ±500 are rejected.", align="justify")
R.figure("calibration_seq", "Remote calibration with confirmation.")
R.h2("Settings in flash")
R.p("All parameters are stored in one structure in the ESP32 non-volatile storage (NVS). The old EEPROM emulation was replaced because it had no "
    "integrity check. The protection against a reset or power loss during a write has several layers:", align="justify")
R.bullets([
    "the NVS library writes a new entry and invalidates the old one afterwards (wear levelling, per-entry CRC);",
    "the whole structure is stored **twice** (slot A and slot B) with magic number, version, size and CRC32;",
    "every write is **read back and compared**; nothing is written when the values did not change;",
    "at boot the first valid slot is used and a damaged slot is repaired from the good one; if both are invalid the factory defaults are used;",
    "the unit reports the **reset reason** (power-on, external reset, software, panic, watchdog, brownout) in the `status` command, which shows whether a restart came from a watchdog, a crash or the supply.",
])
R.figure("flash_write", "Writing and loading the settings.")
R.callout("A complete power loss in the middle of a flash operation cannot be excluded by software alone; the measures above make a corrupted configuration very unlikely and guarantee a valid configuration after boot.", "note")
R.h2("Watchdogs and recovery")
R.p("The ESP32 contains several hardware watchdog timers inside the chip, so **no external watchdog circuit is needed**. "
    "A watchdog is a timer that must be restarted (“fed”) regularly by the software; if it is not, the system assumes that the program "
    "hangs and resets the chip.", align="justify")
R.table(["Watchdog", "What it detects", "Use in the project"], [
    ["Task watchdog (TWDT)", "a subscribed task that does not feed it in time; the idle task of every core is watched as well (a task that never blocks starves the idle task)",
     "timeout 10 s, a timeout resets the chip (`i2WdtInit`); the state machine of the sensor unit and the display task of the matrix unit subscribe and feed it in every loop"],
    ["Interrupt watchdog", "interrupts disabled for too long (for example a stuck critical section)", "normally active in the Arduino-ESP32 build, no code needed"],
    ["RTC / system watchdog", "a stuck start-up", "active by default"],
    ["Brownout detector", "supply voltage drops below a threshold", "resets the chip before flash writes or the CPU become unreliable"],
], widths=[2.8, 5, 5], caption="Internal watchdogs of the ESP32.", font=8.5)
R.p("The recovery measures form a chain: the watchdogs handle a frozen task or a crash; the WiFi supervisor handles a lost connection (reconnect, WiFi stack reset, restart); "
    "the matrix unit restarts when no data arrive for 30 minutes. After every restart the command `status` shows the **reset reason** (power-on, software, panic, "
    "task or interrupt watchdog, brownout, external reset), which helps to find the cause of an unexpected restart.", align="justify")
R.callout("The internal watchdogs cannot help if the whole chip or its supply fails. An external watchdog circuit (for example a 555-based one) could be added later "
          "if a policy requires it; the firmware does not depend on it.", "note")
R.h2("WiFi supervisor, service network and configuration portal")
R.p("One task (`wifiTask`) owns the connection. The automatic reconnect of the WiFi library is switched off so that two mechanisms do not fight each other. "
    "The task tries the configured network first and then the **service network** (a phone hotspot with a factory name and password), which makes over-the-air updates possible "
    "even when the school network is unavailable. If both fail for more than 60 s a **configuration portal** is opened. "
    "The recovery escalates: new attempts, then a restart of the WiFi stack after 2 minutes and a restart of the ESP32 after 10 minutes.", align="justify")
R.figure("wifi_flow", "Connection supervisor.")
R.h3("Configuration portal and access code")
R.p("The portal is the WiFiManager web page: the ESP32 opens a WPA2-protected access point, the user connects with a phone, and a page opens "
    "(`192.168.4.1`) with the list of networks, the password field and the extra fields (channel ID, API key, fast mode) and an **access code**. "
    "The portal is opened only after a longer connection loss (at most once per 15 minutes) or on demand with the console command `portal`, it closes "
    "after 3 minutes, and **nothing is saved unless the access code is correct**. The same code is required for parameter changes on the serial console (`login`).", align="justify")
R.figure("portal_seq", "Configuration portal with access code.")
R.p("**Over-the-air update (OTA).** ArduinoOTA is active on the configured and on the service network and is protected with a password. "
    "During an update the progress is shown on the OLED.", align="justify")
R.h2("Serial console")
R.table(["Command", "Login", "Function"], [
    ["`help`, `status`, `show`", "no", "commands, uptime, reset reason, WiFi state, FSM, ThingSpeak, settings (secrets masked)"],
    ["`login <code>`, `logout`", "-", "unlock for 5 minutes; 3 wrong tries lock the console for 30 s"],
    ["`setup`", "yes", "wizard; shows the current value of every parameter, ENTER keeps it"],
    ["`set <key> <value>`", "yes", "change one parameter (ssid, pass, channel, wkey, rkey, cfgch, cfgkey, mode, sleep, calibration, slmoff)"],
    ["`fast`, `normal`", "yes", "measuring mode"],
    ["`portal`", "yes", "open the configuration access point now"],
    ["`passwd <new>`, `restart`, `factory yes`", "yes", "change the access code, reboot, restore factory values"],
    ["`cal`, `cal fetch`, `cal accept|reject`, `cal auto on|off`", "partly", "calibration"],
    ["`perf`, `perf reset`, `perf cpu on|off`, `perf load ...`", "no", "timing tools (Chapter 7)"],
    ["`slm ...`", "partly", "sound level meter (separate report)"],
], widths=[4.5, 1.3, 6.5], caption="Console commands.")
R.p("The channel ID is validated as 6 to 8 digits (it was 7 digits when the system was built; the number of ThingSpeak channels keeps growing and older channels have 6 digits) "
    "and stored as a number. API keys must have exactly 16 letters or digits.", align="justify")
R.h2("Matrix display unit")
R.figure("matrix_flow", "Matrix unit: ThingSpeak read task, queue and display task.")
R.p("The `ts_read` task reads all fields in **one** HTTP request every 30 s (10 s after a failure) and writes the values into a queue of length 1 with "
    "`xQueueOverwrite`, so the display always receives the newest reading. The `display` task scrolls the text with a step of 60 ms. "
    "If new data arrive while a message is scrolling, they are shown only after the message has left the display. "
    "While the connection is down the text “Connecting..” is shown. If WiFi is up but no reading succeeded for 30 minutes, the unit restarts.", align="justify")
R.p("**Why the display used to stay on “Connecting..”.** The earlier code called `WiFi.reconnect()` in a loop, which does not recover from some "
    "failure states of the WiFi stack. The shared supervisor now starts a new connection attempt, falls back to the service network, resets the WiFi stack after "
    "2 minutes and restarts the chip after 10 minutes. The display shows “Connecting..” only while the link is really down.", align="justify")
R.h3("Display colours")
R.table(["Value", "Green / lime", "Yellow", "Orange", "Red / blue"], [
    ["PM2.5 [µg/m³]", "< 35", "35 - 52", "53 - 69", "≥ 70"],
    ["PM10 [µg/m³]", "< 50", "50 - 74", "75 - 99", "≥ 100"],
    ["Temperature [°C]", "-", "10 - 19", "20 - 29", "< 10 blue; ≥ 30 red"],
    ["Humidity [%]", "< 30 (lime)", "30 - 59", "≥ 60", "-"],
    ["Noise [dBA]", "< 55", "55 - 69", "70 - 84", "≥ 85"],
], widths=[3, 2.4, 2.4, 2.4, 3.2], caption="Colour thresholds of the display.")
R.h2("Timing and measurement tools")
R.p("The library `I2Perf` provides the tools for the real-time analysis (Chapter 7): microsecond **probes** around critical code (count, last, "
    "minimum, average, maximum and the number of deadline overruns), **counters** for events (dropped queue items, failed uploads), "
    "a **CPU load** measurement per core with peak value (idle-hook method, reference taken at boot), a table of all tasks with priority, state and the "
    "**minimum free stack**, the **heap** (free, lowest ever, largest block) and a **load generator** that burns a chosen percentage of a core. "
    "No special build options are needed.", align="justify")

# =============================================================================
R.h1("Changes compared with the earlier versions")
R.h2("Development history")
R.figure("versions", "Development history of the firmware (to be confirmed).")
R.callout("The dates and contents of the early versions in the figure and in the table below are reconstructed from the project folders and "
          "must be confirmed by the author.", "check")
R.table(["Date", "Folder / file", "Content"], [
    ["29.09", "`PMS_DISP.ino`", "first version: PMS sensor and display"],
    ["30.09", "`PMS_DISP_TS.ino`", "ThingSpeak upload added"],
    ["05.10", "`PMS_DISP_TS_OTA.ino`", "over-the-air update added"],
    ["13.10", "`PMS_DISP_TS.ino`", "[to be confirmed: see comment file of 13.10]"],
    ["18.10", "`PMS_DISP_TS.ino`, `MATRIX_DISP2.ino`", "[to be confirmed] matrix display unit"],
    ["28.10", "`PMS_DISP_TS_EEPROM_FIX.ino`, `INMP441_spl.ino`", "state machine, WiFiManager, EEPROM, first microphone and FFT experiment"],
    ["07.11", "block diagrams (presentation)", "design of the RTOS structure"],
    ["16.11", "current repository", "shared library, flash settings, supervisor, portal with access code, calibration, sound level meter, measurement tools"],
], widths=[1.6, 5, 6], caption="Development steps (from the project folders).")
R.h2("Comparison of the last earlier version and the current version")
R.table(["Topic", "Earlier version (28.10)", "Current version", "Why"], [
    ["Code structure", "one large `.ino` file per unit, global variables", "shared library + sketch split into files by function", "readability, reuse by two units, documentation"],
    ["PMS reading", "`find(0x42)` with 1 s timeout inside the state machine", "non-blocking byte parser with length and checksum check", "no blocking, resynchronisation after errors"],
    ["Upload", "HTTP request called directly from the state machine", "separate task, queue, retries, rate limit", "network delay no longer stops the sensor logic"],
    ["States", "POWERON, GETDATA, POWEROFF, IDLE with `millis()` polling", "WARMUP, MEASURE, SLEEP; event-driven wait; sleep time configurable", "simpler cycle, immediate reaction to mode change"],
    ["Sleep in fast mode", "idle time forced to 0 in a separate branch", "SLEEP state is skipped, sensor stays on", "clear semantics"],
    ["Configuration storage", "EEPROM emulation, 128 bytes, key + 3 fields, no CRC", "NVS, two CRC-protected slots, read-back", "protection against reset and corruption"],
    ["Channel ID", "read as exactly 7 characters; 6-7 digits accepted by the form", "6 - 8 digits, stored as number", "channel numbers grow (3.2 million) and old ones have 6 digits"],
    ["WiFi", "WiFiManager `autoConnect` blocks in `setup`; reconnect every 10 s inside the state machine", "supervisor task: configured network, service network, portal, escalation", "recover without manual reset, firmware update always possible"],
    ["Portal security", "access point always used for configuration, no code", "portal opens only when needed, WPA2, 3 min, **access code**", "stop unauthorised changes"],
    ["Serial console", "`fast`, `normal`, `show`, `setkey`", "login with access code, wizard, `set`, calibration, performance commands", "installation and diagnostics"],
    ["Calibration", "coefficients hard-coded in the sensor header", "stored in flash, remote update with confirmation", "sensor replacement without new firmware"],
    ["Matrix WiFi", "SSID, password, channel and key hard-coded; `WiFi.reconnect()` loop", "settings in flash, shared supervisor, restart on long outage", "“Connecting..” problem"],
    ["Matrix data", "four separate HTTP requests, integer fields", "one request for all fields, noise added", "less traffic, more data"],
    ["Matrix scrolling", "message width computed from a different text than printed", "width from the text actually drawn", "message could be cut off"],
    ["Tasks on ESP32", "all tasks on core 1, equal priorities", "priorities by urgency, cores assigned, single-core aware", "predictable timing, S2 compatibility"],
    ["Measurement", "none", "probes, CPU load, stack/heap, load generator", "real-time analysis"],
    ["Sound level", "16 kHz, 512 samples, no weighting, empirical offset, spectrum printed in every loop", "48 kHz, A-weighting, calibrated, tasks, window for ThingSpeak (separate report)", "meaningful dBA values, real-time"],
], widths=[2.3, 3.8, 3.9, 3.0], caption="What was changed and why.", font=8.5)
R.h2("Problems found in the earlier code and how they were handled")
R.bullets([
    "**Blocking in the state machine** (`find()` timeout, HTTP request): removed by the non-blocking parser and the upload task.",
    "**Channel ID length mismatch** between the web form, the EEPROM layout and the validation: replaced by one validator and a numeric field.",
    "**Missing string terminator** in the stored write key (found earlier): the new storage uses fixed-size, zero-terminated fields and a CRC.",
    "**Unprotected configuration access point**: protected with access code and opened only when needed.",
    "**WiFi recovery**: the matrix display could stay on “Connecting..”; a supervisor with escalation replaces `WiFi.reconnect()`.",
    "**Fast mode persisted** in flash: documented in the pre-deployment checklist.",
])

# =============================================================================
R.h1("Testing")
R.h2("Test set-up")
R.p("A complete test needs no school hardware: an ESP32 board with the PMS5003T and an OLED (sensor unit), a second ESP32 with a 16 x 8 matrix "
    "(matrix unit), optionally the INMP441, a phone (hotspot, portal, reference sound-level app) and a ThingSpeak account with a data channel "
    "and optionally a calibration channel. Serial monitor: 115200 baud, line ending Newline.", align="justify")
R.h2("Test list")
R.table(["#", "Test", "Expected result", "Result"], [
    ["1", "Boot of the sensor unit", "factory-defaults message on the first boot, idle baseline, WiFi connected, `Setup done`", "[ ]"],
    ["2", "Console lock", "`set` without login is refused", "[ ]"],
    ["3", "Login and validation", "5-digit channel rejected, 6-digit accepted, wrong key length rejected", "[ ]"],
    ["4", "Setup wizard", "current values shown, ENTER keeps them", "[ ]"],
    ["5", "Persistence", "values survive a reset; `status` shows “settings from flash”", "[ ]"],
    ["6", "Wrong access code", "third wrong try locks the console for 30 s", "[ ]"],
    ["7", "State machine timing", "WARMUP 30 s, MEASURE 30 s with one sample per second, SLEEP 240 s", "[ ]"],
    ["8", "Sensor reaction", "PM values rise when smoke or breath reaches the sensor", "[ ]"],
    ["9", "Fast mode", "sleep is interrupted at once, no sleep afterwards, LED 250 ms", "[ ]"],
    ["10", "ThingSpeak upload", "fields 1 - 4 updated, `upload OK`", "[ ]"],
    ["11", "Upload failure", "wrong write key: 3 retries, then `giving up`, counter increases", "[ ]"],
    ["12", "Sensor unplugged", "`no valid frames` message and counter", "[ ]"],
    ["13", "WiFi loss and return", "link lost message, automatic reconnection", "[ ]"],
    ["14", "Service network", "unit joins the phone hotspot when the main network is off", "[ ]"],
    ["15", "Configuration portal", "access point appears after 60 s or on `portal`; wrong code saves nothing; right code saves", "[ ]"],
    ["16", "Outage of more than 10 minutes", "unit restarts itself and keeps trying", "[ ]"],
    ["17", "OTA update", "update over the network port, progress on the OLED, wrong password refused", "[ ]"],
    ["18", "Remote calibration", "pending message, accept applies, older or invalid versions ignored", "[ ]"],
    ["19", "Matrix unit", "scrolling coloured text, “Connecting..” while offline, recovery without reset", "[ ]"],
], widths=[0.8, 3.4, 7.2, 1.2], caption="Functional tests (tick when passed).", font=8.5, align_center_cols=(0, 3))
R.h2("Remote calibration test")
R.numbered([
    "Create a second ThingSpeak channel; on the unit set `cfgch` and `cfgkey`.",
    "Write field 1 from a browser: `https://api.thingspeak.com/update?api_key=<WRITE_KEY>&field1=2,1.10,0,1.00,0,1,-1.5,1,3`.",
    "`cal fetch` reports a new version; `cal` shows active and pending values; `login` and `cal accept` apply them.",
    "From the next sample PM2.5 is 10 % higher, the temperature 1.5 °C lower and the humidity 3 % higher.",
    "Write an older version or a broken string: it must be ignored with a message.",
])
R.h2("Pre-deployment checklist")
R.bullets([
    "`normal` mode and sleep time 240 s (`show`).",
    "Access code, OTA password and portal password changed from the factory values.",
    "`status`: WiFi connected, upload OK, no pending calibration, reset reason power-on.",
    "`ENABLE_SLM 0` if no microphone is fitted, `ENABLE_OLED 0` if no OLED is fitted.",
    "The file with passwords (`i2_secrets.h`) is not stored in version control.",
])

# =============================================================================
R.h1("Performance and real-time analysis")
R.h2("Method")
R.p("The goal is to find out whether the processor can run the normal flow (state machine, upload, display) **and** the sound level meter at the "
    "same time, how large the load is, where the limit is and what to do if it is reached. The following quantities are measured with the "
    "`perf` command (see Section 4.12):", align="justify")
R.table(["Quantity", "Probe / source", "Meaning"], [
    ["Execution time C of a code section", "`pms_loop_body`, `slm_block`, `slm_calc`, `slm_fft`, `oled_frame`, `matrix_frame`, `ts_http`", "worst case = maximum value"],
    ["Wake-up lateness (jitter)", "`pms_wake_late`", "how late the state machine starts after its 50 ms wait"],
    ["Deadline overruns", "overrun column of a probe", "number of times C exceeded its budget (period)"],
    ["CPU load per core", "`perf cpu on`", "current, average and peak load, 1 s windows"],
    ["Free stack of every task", "task table", "stack overflow danger below about 300 bytes"],
    ["Heap", "heap line", "free, lowest ever, largest block"],
    ["Dropped data", "counters", "queue full events (`slm_block_dropped`, `ts_queue_dropped`)"],
], widths=[3.5, 5, 3.5], caption="Measured quantities.")
R.h2("Configurations")
R.table(["Configuration", "How", "Purpose"], [
    ["A: sensor only", "`ENABLE_SLM 0`", "reference load of the normal flow"],
    ["B: sensor + sound level meter, two cores", "defaults", "real operating condition of the development board"],
    ["C: sensor + sound level meter, one core", "`TEST_SINGLE_CORE 1`", "imitates the ESP32-S2 mini"],
], widths=[4, 3.5, 4.5], caption="Measurement configurations.")
R.h2("Overload experiment")
R.p("The command `perf load <pct> <prio> <core> <seconds>` creates a task that uses the chosen percentage of a core in 100 ms periods. "
    "The load is increased step by step (30, 50, 70, 90 %) at a low and at a high priority; after every step the report is read. "
    "The interpretation is given below.", align="justify")
R.table(["Observation", "Meaning"], [
    ["`slm_block` maximum below its budget (125 000 µs)", "the filter chain finishes within one block period"],
    ["`slm_block` overruns > 0", "deadline missed - the I2S buffers can overflow"],
    ["`slm_block_dropped` > 0", "the calculation task cannot keep up, data are lost"],
    ["`pms_wake_late` maximum grows", "scheduling jitter of the state machine under load"],
    ["free stack below 300 bytes", "increase the stack of that task"],
    ["WiFi drops at very high load", "a high-priority load task starves the network task"],
], widths=[5, 7], caption="Interpretation of the results.")
R.h2("Results")
R.callout("The tables below are to be filled in after the measurements on the bench set-up. The numbers are intentionally left empty - no value is assumed.", "todo")
R.table(["Probe", "Config A avg / max [µs]", "Config B avg / max [µs]", "Config C avg / max [µs]", "Budget [µs]"], [
    ["`pms_loop_body`", "", "", "", "50 000"],
    ["`pms_wake_late`", "", "", "", "50 000"],
    ["`slm_block`", "-", "", "", "125 000"],
    ["`slm_calc`", "-", "", "", "125 000"],
    ["`oled_frame`", "", "", "", "250 000"],
    ["`ts_http`", "", "", "", "-"],
], widths=[2.8, 2.8, 2.8, 2.8, 2], caption="Execution times (to be measured).", font=8.5)
R.table(["Quantity", "Config A", "Config B", "Config C"], [
    ["CPU load core 0: average / peak [%]", "", "", ""],
    ["CPU load core 1: average / peak [%]", "", "", "-"],
    ["Minimum free heap [bytes]", "", "", ""],
    ["Smallest free stack (task) [bytes]", "", "", ""],
    ["Load at which the first deadline is missed [%]", "", "", ""],
], widths=[5.5, 2.2, 2.2, 2.2], caption="Load, memory and breaking point (to be measured).", font=8.5)
R.h2("Counter-measures if the limit is reached")
R.bullets([
    "lower the priority of non-critical tasks or move them to the other core;",
    "distribute the calculation in time (smaller blocks, processing in slices);",
    "reduce the work per sample (fewer filter sections, fixed-point arithmetic, optimised library functions);",
    "run the FFT only on request;",
    "reduce the sampling rate or the update rate of the display.",
])
R.callout("This analysis is the basis for the following subject, optimisation in C, where the critical sections found here are optimised and measured again.", "note")

# =============================================================================
R.h1("Conclusions and further work")
R.p("The firmware of the sensor and display units was restructured from a super-loop into FreeRTOS tasks with queues, mutexes and notifications. "
    "The state machine is deterministic and event-driven, the network can no longer block the sensor logic, the configuration is protected against "
    "corruption and unauthorised changes, the WiFi connection recovers by itself, calibration can be updated without a new firmware, and the "
    "real-time behaviour can be measured. The code builds for the ESP32 and is prepared for the single-core ESP32-S2.", align="justify")
R.p("**Further work:** hardware verification of all tests, filling in the measurement tables, verification of the PMS5003T frame offsets, "
    "adaptation of the pin definitions for the ESP32-S2 mini, encrypted transport (HTTPS) to ThingSpeak, and optimisation of the critical sections.", align="justify")

# =============================================================================
R.h1("Appendices", numbered=False)
R.h2("A. Main configuration constants")
R.table(["Constant", "Value", "Meaning"], [
    ["`PMS_WARMUP_MS`", "30 000", "warm-up time"],
    ["`PMS_MEASURE_MS`", "30 000", "measuring window (normal mode)"],
    ["`PMS_FAST_WINDOW_MS`", "60 000", "window between uploads in fast mode"],
    ["`PMS_SLEEP_DEFAULT_S`", "240", "sleep time (changeable, 10 - 3600 s)"],
    ["`PMS_LOOP_MS`", "50", "period of the state machine"],
    ["`LED_NORMAL_MS` / `LED_FAST_MS`", "500 / 250", "LED toggle period"],
    ["`TS_MIN_GAP_MS`, `TS_RETRIES`, `TS_RETRY_WAIT_MS`", "16 000, 3, 20 000", "upload limits"],
    ["`CAL_CHECK_PERIOD_MS`", "3 600 000", "remote calibration check"],
    ["`SCROLL_DELAY_MS` (matrix)", "60", "scroll step"],
    ["`TS_READ_MS` (matrix)", "30 000", "reading period"],
    ["`NO_DATA_RESTART_MS` (matrix)", "1 800 000", "restart if no data"],
], widths=[5, 3, 5], caption="Constants in `app_config.h`.")
R.h2("B. References")
R.bullets([
    "FreeRTOS documentation and book “Mastering the FreeRTOS Real Time Kernel” - https://www.freertos.org",
    "ESP-IDF Programming Guide, FreeRTOS (IDF) - https://docs.espressif.com/projects/esp-idf/",
    "Arduino-ESP32 documentation - https://docs.espressif.com/projects/arduino-esp32/",
    "Espressif datasheets: ESP32, ESP32-WROOM-32, ESP32-S2 - https://www.espressif.com",
    "Plantower PMS5003T datasheet (manufacturer)",
    "ThingSpeak documentation - https://www.mathworks.com/help/thingspeak/",
    "WiFiManager library - https://github.com/tzapu/WiFiManager",
    "Adafruit NeoMatrix / NeoPixel libraries and the “NeoPixel Überguide” - https://learn.adafruit.com",
    "WS2812B datasheet (WorldSemi)",
    "C. L. Liu, J. W. Layland: Scheduling Algorithms for Multiprogramming in a Hard-Real-Time Environment, JACM 1973",
])
R.callout("Add further resources, standards (for example the guideline values for PM2.5 / PM10) and the school's reference texts here.", "todo")

R.save(OUT)
