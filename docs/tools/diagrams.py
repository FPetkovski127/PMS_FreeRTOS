"""Figure generator for the two Word reports (light, print friendly style).

Run:  python diagrams.py          (writes PNG files into ../figures)
All coordinates: x = 0..100 units over a 7.5 inch wide canvas (1 unit = 0.075 in).
"""
import os
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, Polygon, Rectangle

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "figures")
os.makedirs(OUT, exist_ok=True)

plt.rcParams["font.family"] = "DejaVu Sans"

# palette: (face, edge)
BLUE = ("#dbeafe", "#1d4ed8")
GREEN = ("#dcfce7", "#15803d")
GREY = ("#f3f4f6", "#4b5563")
PURPLE = ("#ede9fe", "#6d28d9")
AMBER = ("#fef3c7", "#b45309")
RED = ("#fee2e2", "#b91c1c")
WHITE = ("#ffffff", "#111827")
TXT = "#111827"


def canvas(h_in, w_in=7.5):
    fig = plt.figure(figsize=(w_in, h_in), dpi=200)
    ax = fig.add_axes([0, 0, 1, 1])
    ax.set_xlim(0, 100)
    ax.set_ylim(0, 100 * h_in / w_in)
    ax.axis("off")
    return fig, ax


def save(fig, name):
    fig.savefig(os.path.join(OUT, name + ".png"), dpi=200, facecolor="white", bbox_inches="tight", pad_inches=0.06)
    plt.close(fig)


def box(ax, x, y, w, h, text, col=BLUE, fs=7.5, bold=False, ls="-", lw=1.1, tc=TXT, rounding=1.0, z=3):
    p = FancyBboxPatch((x, y), w, h, boxstyle=f"round,pad=0,rounding_size={rounding}",
                       fc=col[0], ec=col[1], lw=lw, ls=ls, zorder=z)
    ax.add_patch(p)
    if text:
        ax.text(x + w / 2, y + h / 2, text, ha="center", va="center", fontsize=fs, color=tc,
                fontweight="bold" if bold else "normal", zorder=z + 1, linespacing=1.25)


def frame(ax, x, y, w, h, title, col=GREY, fs=8):
    p = FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0,rounding_size=1.5", fc="none", ec=col[1],
                       lw=1.3, ls="--", zorder=1)
    ax.add_patch(p)
    ax.text(x + 1.5, y + h - 1.6, title, ha="left", va="top", fontsize=fs, color=col[1], fontweight="bold", zorder=2)


def arrow(ax, p1, p2, text="", col="#374151", ls="-", lw=1.1, both=False, fs=6.8, off=(0, 1.2), tbox=True, rad=0.0):
    style = "<|-|>" if both else "-|>"
    ax.annotate("", xy=p2, xytext=p1, zorder=5,
                arrowprops=dict(arrowstyle=style, color=col, lw=lw, ls=ls, shrinkA=0, shrinkB=0,
                                connectionstyle=f"arc3,rad={rad}", mutation_scale=9))
    if text:
        mx, my = (p1[0] + p2[0]) / 2 + off[0], (p1[1] + p2[1]) / 2 + off[1]
        ax.text(mx, my, text, ha="center", va="center", fontsize=fs, color=col, zorder=6,
                bbox=dict(fc="white", ec="none", pad=0.6) if tbox else None)


def diamond(ax, cx, cy, w, h, text, col=AMBER, fs=7):
    pts = [(cx, cy + h / 2), (cx + w / 2, cy), (cx, cy - h / 2), (cx - w / 2, cy)]
    ax.add_patch(Polygon(pts, closed=True, fc=col[0], ec=col[1], lw=1.1, zorder=3))
    ax.text(cx, cy, text, ha="center", va="center", fontsize=fs, color=TXT, zorder=4, linespacing=1.2)


def label(ax, x, y, text, fs=7, ha="center", col=TXT, bold=False, style="normal"):
    ax.text(x, y, text, ha=ha, va="center", fontsize=fs, color=col, zorder=6,
            fontweight="bold" if bold else "normal", fontstyle=style, linespacing=1.25)


def sequence(ax, actors, msgs, top, bottom, x0=8, x1=92, fs=6.8, notes=()):
    """actors: list of names; msgs: list of (src, dst, text) or ('note', idx, text)."""
    n = len(actors)
    xs = [x0 + i * (x1 - x0) / (n - 1) for i in range(n)]
    for x, name in zip(xs, actors):
        box(ax, x - 9.5, top, 19, 5.5, name, GREY, fs=fs, bold=True)
        ax.plot([x, x], [top, bottom], color="#9ca3af", lw=0.9, ls="--", zorder=1)
    y = top - 4.2
    step = (top - bottom - 4) / max(len(msgs), 1)
    step = min(step, 6.2)
    for m in msgs:
        if m[0] == "note":
            x = xs[m[1]]
            box(ax, x - 22, y - 2.2, 44, 4.4, m[2], AMBER, fs=fs - 0.3)
        elif m[0] == "self":
            x = xs[m[1]]
            ax.text(x + 1.5, y, m[2], ha="left", va="center", fontsize=fs, color="#374151",
                    bbox=dict(fc="#fef3c7", ec="#b45309", pad=1.2, lw=0.6))
        else:
            a, b, t = m
            arrow(ax, (xs[a], y), (xs[b], y), t, fs=fs, off=(0, 1.9))
        y -= step
    return xs


# ----------------------------------------------------------------------------
#  REPORT 1  -  system, hardware
# ----------------------------------------------------------------------------
def fig_system_overview():
    fig, ax = canvas(4.4)
    frame(ax, 1, 14, 40, 41, "Sensor unit (ESP32)")
    box(ax, 4, 38, 15, 7, "PMS5003T\nPM2.5 / PM10\nT / RH", GREEN, fs=6.8)
    box(ax, 4, 28, 15, 7, "INMP441\nmicrophone", GREEN, fs=6.8)
    box(ax, 4, 18, 15, 7, "OLED\n(bench only)", GREEN, fs=6.8)
    box(ax, 24, 20, 15, 22, "ESP32\nFreeRTOS\nsensor\nfirmware", BLUE, fs=7.5, bold=True)
    arrow(ax, (19, 41.5), (24, 36), "UART", fs=6)
    arrow(ax, (19, 31.5), (24, 31), "I2S", fs=6)
    arrow(ax, (24, 22), (19, 21.5), "I2C", fs=6)

    box(ax, 47, 33, 22, 12, "ThingSpeak\ndata channel\n(fields 1-6)", PURPLE, fs=7.5, bold=True)
    box(ax, 47, 12, 22, 10, "ThingSpeak\ncalibration channel\n(field 1, optional)", PURPLE, fs=7)
    arrow(ax, (39, 36), (47, 38), "HTTP write\n(every ~5 min)", fs=6)
    arrow(ax, (47, 17), (39, 24), "HTTP read\n(hourly)", fs=6)

    frame(ax, 76, 3, 23, 52, "Matrix display unit")
    box(ax, 79, 26, 17, 18, "ESP32\nFreeRTOS\nmatrix\nfirmware", BLUE, fs=7.5, bold=True)
    box(ax, 79, 7, 17, 10, "NeoPixel\nLED matrix\n(16x8 .. 64x8)", GREEN, fs=6.8)
    arrow(ax, (69, 39), (79, 37), "HTTP read\n(every 30 s)", fs=6)
    arrow(ax, (87.5, 26), (87.5, 17), "DIN", fs=6)

    box(ax, 47, 47, 22, 7, "Phone / PC: OTA update,\nconfig portal, serial console", AMBER, fs=6.3)
    arrow(ax, (47, 50), (39, 44), "", ls="--", col="#b45309")
    arrow(ax, (69, 50), (80, 44), "", ls="--", col="#b45309")
    save(fig, "system_overview")


def fig_sensor_hw():
    fig, ax = canvas(4.2)
    box(ax, 36, 8, 28, 44, "ESP32-WROOM-32\n(NodeMCU-32S, CH340)\n\n3.3 V logic\ntwo cores, 240 MHz", BLUE, fs=8, bold=True)
    rows = [
        (46, "GPIO16 (RX2)", "PMS5003T  TX", 1),
        (40, "GPIO17 (TX2)", "PMS5003T  RX", 1),
        (34, "GPIO4", "PMS5003T  SET (fan on/off)", 1),
        (28, "GPIO21 (SDA)", "OLED SSD1306  SDA", 1),
        (22, "GPIO22 (SCL)", "OLED SSD1306  SCL", 1),
    ]
    for y, pin, dev, _ in rows:
        box(ax, 2, y - 2.5, 30, 5, dev, GREEN, fs=6.8)
        arrow(ax, (32, y), (36, y), "", both=True)
        label(ax, 34, y + 3.0, pin, fs=5.6, col="#1d4ed8")
    rows2 = [
        (46, "GPIO32", "INMP441  SCK (bit clock)"),
        (40, "GPIO5", "INMP441  WS (word select)"),
        (34, "GPIO33", "INMP441  SD (data)"),
        (28, "GPIO2", "status LED"),
    ]
    for y, pin, dev in rows2:
        box(ax, 68, y - 2.5, 30, 5, dev, GREEN, fs=6.8)
        arrow(ax, (64, y), (68, y), "", both=True)
        label(ax, 66, y + 3.0, pin, fs=5.6, col="#1d4ed8")
    label(ax, 50, 3.5, "Power: PMS5003T 5 V (3.3 V logic), INMP441 3.3 V with L/R pin to GND, OLED 3.3 V", fs=6.5)
    save(fig, "sensor_hw")


def fig_matrix_hw():
    fig, ax = canvas(3.5)
    box(ax, 2, 20, 17, 16, "ESP32\n(matrix unit)\n3.3 V logic", BLUE, fs=7.5, bold=True)
    box(ax, 24, 21, 11, 14, "level shifter\n3.3 V -> 5 V\n(74HCT125)\nrecommended", AMBER, fs=5.8)
    panels = []
    for i in range(4):
        x = 40 + i * 14.5
        box(ax, x, 18, 12.5, 20, f"16 x 8\npanel {i+1}\n128 LEDs\nWS2812B", GREEN, fs=6.3)
        panels.append(x)
    arrow(ax, (19, 28), (24, 28), "GPIO5", fs=5.8, off=(0, 2))
    arrow(ax, (35, 28), (40, 28), "DIN", fs=5.8, off=(0, 2))
    for i in range(3):
        arrow(ax, (panels[i] + 12.5, 28), (panels[i + 1], 28), "DOUT\n-> DIN", fs=5.2, off=(0, 3.5))
    box(ax, 2, 4, 40, 7, "5 V power supply (power injected at each panel,\ncommon GND with the ESP32)", RED, fs=6.3)
    label(ax, 76, 8.5, "Four 16x8 panels in a chain = 64 x 8 display (512 LEDs)\nZig-zag column wiring, origin top-left", fs=6.3)
    for x in panels:
        ax.plot([x + 6, x + 6], [18, 11], color="#b91c1c", lw=1, ls="--")
    ax.plot([42, 100 - 6], [11, 11], color="#b91c1c", lw=1, ls="--")
    save(fig, "matrix_hw")


# ----------------------------------------------------------------------------
#  RTOS theory figures
# ----------------------------------------------------------------------------
def fig_superloop_vs_rtos():
    fig, ax = canvas(4.4)
    label(ax, 50, 56, "Same work, two execution models", fs=8.5, bold=True)
    label(ax, 2, 49, "Super-loop", fs=7.5, ha="left", bold=True)
    seq = [("read\nsensor", BLUE, 10), ("HTTP upload\n(blocks 1-3 s)", RED, 24), ("update\ndisplay", GREEN, 9),
           ("check\nbuttons", GREY, 8), ("read\nsensor", BLUE, 10), ("HTTP upload\n(blocks)", RED, 20)]
    x = 14
    for t, c, w in seq:
        box(ax, x, 42, w, 10, t, c, fs=5.6, rounding=0.5)
        x += w
    arrow(ax, (14, 39.5), (96, 39.5), "", lw=0.8)
    label(ax, 56, 37, "time  (everything waits for the previous step; a slow step delays all others)", fs=6, col="#6b7280")

    label(ax, 2, 28, "RTOS", fs=7.5, ha="left", bold=True)
    names = ["Sensor task (prio 3)", "Upload task (prio 2)", "Display task (prio 1)"]
    cols = [BLUE, RED, GREEN]
    for i, (n, c) in enumerate(zip(names, cols)):
        y = 22 - i * 8
        label(ax, 13, y + 2, n, fs=5.8, ha="right")
        ax.plot([14, 98], [y, y], color="#d1d5db", lw=0.8)
    # sensor task slices
    for xs in (16, 34, 52, 70, 88):
        box(ax, xs, 21, 4, 4, "", BLUE, rounding=0.3)
    box(ax, 20, 13, 38, 4, "", RED, rounding=0.3)
    label(ax, 39, 15, "HTTP (blocked, waiting for network)", fs=5.4)
    for xs in (22, 40, 58, 76):
        box(ax, xs, 5, 6, 4, "", GREEN, rounding=0.3)
    arrow(ax, (14, 2.2), (98, 2.2), "", lw=0.8)
    label(ax, 56, 0.4, "time  (the scheduler always runs the most urgent ready task; waiting tasks use no CPU)", fs=6, col="#6b7280")
    save(fig, "superloop_vs_rtos")


def fig_task_states():
    fig, ax = canvas(3.4)
    box(ax, 38, 34, 24, 9, "RUNNING\n(on a CPU core)", GREEN, fs=7.5, bold=True)
    box(ax, 6, 16, 24, 9, "READY\n(waits for a core)", BLUE, fs=7.5, bold=True)
    box(ax, 70, 16, 24, 9, "BLOCKED\n(delay, queue, mutex,\nnotification)", AMBER, fs=7, bold=True)
    box(ax, 38, 3, 24, 7, "SUSPENDED\n(vTaskSuspend)", GREY, fs=6.8)
    arrow(ax, (30, 24), (40, 34), "scheduler selects\nhighest priority", fs=6, off=(9, -3))
    arrow(ax, (44, 34), (24, 25.5), "preempted by a more urgent\ntask / time slice", fs=6, off=(-1, 7), rad=0.35)
    arrow(ax, (62, 38), (82, 25), "waits: vTaskDelay,\nxQueueReceive, xSemaphoreTake", fs=6, off=(10, 2))
    arrow(ax, (76, 16), (24, 16), "event / timeout occurs  (task becomes READY)", fs=6, off=(0, 2.2))
    arrow(ax, (20, 16), (38, 8), "vTaskSuspend", fs=5.8, ls=":", col="#6b7280", off=(8, 1))
    arrow(ax, (38, 5), (14, 16), "vTaskResume", fs=5.8, ls=":", col="#6b7280", off=(-9, -3))
    save(fig, "task_states")


def fig_queue():
    fig, ax = canvas(2.6)
    box(ax, 3, 13, 20, 11, "Producer task\n(e.g. FSM)", BLUE, fs=7.5, bold=True)
    box(ax, 77, 13, 20, 11, "Consumer task\n(e.g. upload)", GREEN, fs=7.5, bold=True)
    for i in range(4):
        box(ax, 33 + i * 8.5, 14, 8, 9, f"item\n{i+1}", PURPLE, fs=6.3, rounding=0.3)
    label(ax, 50, 28, "Queue (FIFO, copies data, fixed length)", fs=7.5, bold=True)
    arrow(ax, (23, 18.5), (33, 18.5), "xQueueSend", fs=6.3, off=(0, 3.4))
    arrow(ax, (67, 18.5), (77, 18.5), "xQueueReceive", fs=6.3, off=(0, 3.4))
    label(ax, 50, 6, "Producer blocks (or drops) when the queue is full, the consumer blocks when it is empty.", fs=6.5, col="#4b5563")
    save(fig, "queue")


def fig_priority_inversion():
    fig, ax = canvas(3.6)
    label(ax, 50, 41.5, "Priority inversion and priority inheritance (mutex)", fs=8, bold=True)
    names = ["High task", "Medium task", "Low task"]
    for i, n in enumerate(names):
        y = 33 - i * 10
        label(ax, 14, y + 1, n, fs=6.5, ha="right")
        ax.plot([15, 98], [y, y], color="#d1d5db", lw=0.8)
    box(ax, 16, 11.3, 16, 3.5, "Low holds mutex", AMBER, fs=5.5, rounding=0.3)
    box(ax, 34, 31.3, 12, 3.5, "High needs mutex", RED, fs=5.3, rounding=0.3)
    label(ax, 34, 27.5, "High is blocked", fs=5.5, ha="left", col="#b91c1c")
    box(ax, 34, 21.3, 30, 3.5, "Medium runs (without inheritance: delays Low, and so High)", BLUE, fs=5.2, rounding=0.3)
    box(ax, 66, 11.3, 12, 3.5, "Low (boosted)", GREEN, fs=5.5, rounding=0.3)
    box(ax, 80, 31.3, 16, 3.5, "High runs", GREEN, fs=5.5, rounding=0.3)
    label(ax, 50, 3, "With a FreeRTOS mutex the Low task temporarily inherits the priority of High, so Medium cannot starve it.", fs=6.2, col="#4b5563")
    save(fig, "priority_inversion")


def fig_layers():
    fig, ax = canvas(3.3)
    box(ax, 5, 28, 42, 9, "PMS_Sensor_RTOS\nFSM - ThingSpeak - SLM - OLED", BLUE, fs=7.5, bold=True)
    box(ax, 53, 28, 42, 9, "Matrix_Display_RTOS\nThingSpeak read - scrolling text", BLUE, fs=7.5, bold=True)
    box(ax, 5, 15, 90, 9, "", PURPLE)
    label(ax, 50, 22, "Library I2Common (shared)", fs=7.5, bold=True)
    for i, t in enumerate(["I2Settings\nflash storage", "I2Wifi\nsupervisor, portal, OTA", "I2Console\nserial commands", "I2Perf\ntiming, CPU, stack"]):
        box(ax, 8 + i * 22, 16.5, 20, 5.2, t, WHITE, fs=5.6, rounding=0.4)
    box(ax, 5, 3, 90, 9, "Arduino-ESP32  +  FreeRTOS  (tasks, queues, mutexes, notifications)  +  drivers: WiFi, NVS flash, I2S, UART, I2C", GREY, fs=6.8)
    arrow(ax, (26, 28), (26, 24), "", both=True)
    arrow(ax, (74, 28), (74, 24), "", both=True)
    arrow(ax, (50, 15), (50, 12), "", both=True)
    save(fig, "layers")


# ----------------------------------------------------------------------------
#  Project figures
# ----------------------------------------------------------------------------






def fig_sensor_tasks():
    fig, ax = canvas(7.4)
    frame(ax, 1, 20, 98, 58, "")
    label(ax, 50, 21.4, "Sensor unit - tasks, queues and shared data (priority, core)", fs=7, bold=True, col="#4b5563")
    # external world
    box(ax, 4, 84, 18, 8, "PMS5003T\nUART 9600", GREEN, fs=6.5)
    box(ax, 41, 84, 18, 8, "ThingSpeak\n(HTTP)", PURPLE, fs=6.5)
    box(ax, 78, 84, 20, 8, "INMP441\nI2S 48 kHz", GREEN, fs=6.5)
    # row 1 (y 56)
    box(ax, 4, 56, 20, 9, "pms_fsm\nprio 3, core 0\nWARMUP-MEASURE-SLEEP", BLUE, fs=6.2, bold=True)
    box(ax, 40, 56, 20, 9, "thingspeak\nprio 2, core 0\nupload, calibration", BLUE, fs=6.2, bold=True)
    box(ax, 74, 56, 18, 9, "slm_reader\nprio 4, core 1\nI2S + IIR filters", BLUE, fs=6.2, bold=True)
    box(ax, 26.5, 59, 10, 4, "g_tsQ", PURPLE, fs=6, ls="--")
    arrow(ax, (24, 61), (26.5, 61))
    arrow(ax, (36.5, 61), (40, 61))
    # row 2 (y 36)
    box(ax, 4, 36, 20, 9, "display / led\nprio 1, core 0\nOLED, status LED", BLUE, fs=6.2)
    box(ax, 40, 36, 20, 9, "wifiTask + console\nprio 2 / 1, core 0\nlibrary tasks", PURPLE, fs=6.2)
    box(ax, 74, 36, 18, 9, "slm_calc\nprio 2, core 1\ndB, Leq, FFT", BLUE, fs=6.2, bold=True)
    box(ax, 81, 47.3, 4, 6, "Q", PURPLE, fs=6, ls="--")
    arrow(ax, (83, 56), (83, 53.3))
    arrow(ax, (83, 47.3), (83, 45))
    # row 3 (y 23)
    box(ax, 4, 23, 20, 8, "latest sample\n(mutex)", PURPLE, fs=6)
    box(ax, 40, 23, 20, 8, "I2Settings (flash)\nmutex protected", PURPLE, fs=6)
    box(ax, 74, 23, 18, 8, "LAeq window\n(mutex)", PURPLE, fs=6)
    arrow(ax, (14, 56), (14, 45), "", ls="--")
    label(ax, 19.5, 50.5, "sample", fs=5.6)
    arrow(ax, (14, 36), (14, 31), "", ls="--")
    arrow(ax, (50, 36), (50, 31), "", both=True)
    arrow(ax, (83, 36), (83, 31), "", ls="--")
    # window -> FSM (slmTakeWindow)
    pts = [(74, 27), (68, 27), (68, 33.5), (27, 33.5), (27, 57.5)]
    ax.plot([p[0] for p in pts], [p[1] for p in pts], color="#6d28d9", lw=1.1, ls="--", zorder=5)
    arrow(ax, (27, 57.5), (24, 57.5), col="#6d28d9")
    label(ax, 47, 34.9, "slmTakeWindow(): LAeq + LAmax at the end of every MEASURE window", fs=5.6, col="#6d28d9")
    # externals
    arrow(ax, (13, 84), (13, 65), "bytes", fs=5.8, off=(3.5, 0))
    arrow(ax, (50, 65), (50, 84), "write / read", fs=5.8, off=(6, 0), both=True)
    ax.plot([96, 96], [84, 60.5], color="#374151", lw=1.1, zorder=5)
    arrow(ax, (96, 60.5), (92, 60.5))
    ax.plot([88, 96], [84, 84], color="#374151", lw=1.1, zorder=5)
    label(ax, 94.2, 72, "DMA\nblocks", fs=5.4)
    # rules
    box(ax, 4, 4, 92, 13, "", WHITE)
    label(ax, 50, 13.6, "Rules used in the design", fs=7, bold=True)
    label(ax, 50, 8.3, "Time-critical work (I2S block every 125 ms) has the highest priority and its own core. The blocking network task is below the FSM.\n"
                       "Tasks never call each other: they exchange data only through queues, mutex-protected variables and notifications.", fs=5.9)
    save(fig, "sensor_tasks")


def fig_fsm():
    fig, ax = canvas(3.3)
    box(ax, 2, 20, 22, 13, "WARMUP\nfan ON\nframes discarded\n30 s", BLUE, fs=6.8, bold=True)
    box(ax, 40, 20, 22, 13, "MEASURE\ncalibrate, sum samples\n30 s (fast: 60 s)", GREEN, fs=6.5, bold=True)
    box(ax, 76, 20, 22, 13, "SLEEP\nfan OFF\n240 s (normal mode)", AMBER, fs=6.8, bold=True)
    arrow(ax, (24, 26.5), (40, 26.5), "30 s elapsed", fs=6, off=(0, 2.6))
    arrow(ax, (62, 26.5), (76, 26.5), "window end:\naverage -> queue", fs=5.8, off=(0, 3.6))
    ax.plot([87, 87], [20, 12], color="#374151", lw=1.1)
    ax.plot([87, 13], [12, 12], color="#374151", lw=1.1)
    arrow(ax, (13, 12), (13, 20))
    label(ax, 50, 9, "sleep elapsed  OR  mode changed to FAST (a task notification wakes the FSM at once)", fs=6.2)
    ax.annotate("", xy=(46, 33), xytext=(56, 33), zorder=5,
                arrowprops=dict(arrowstyle="-|>", color="#15803d", lw=1.2, connectionstyle="arc3,rad=1.6", mutation_scale=9))
    label(ax, 51, 43, "FAST mode: window end -> MEASURE again\n(sensor stays ON, no sleep)", fs=6.2, col="#15803d")
    label(ax, 50, 3, "Normal cycle: 30 s + 30 s + 240 s = 300 s per ThingSpeak update", fs=6.8, bold=True)
    save(fig, "fsm")


def fig_wifi_flow():
    fig, ax = canvas(5.2)
    label(ax, 50, 66, "wifiTask - connection supervisor (runs forever)", fs=8, bold=True)
    box(ax, 3, 52, 22, 8, "Try CONFIGURED\nnetwork (from flash)", BLUE, fs=6.5)
    diamond(ax, 40, 56, 14, 10, "connected?", fs=5.8)
    box(ax, 33, 36, 22, 8, "Try SERVICE network\n(phone hotspot)", BLUE, fs=6.5)
    diamond(ax, 68, 40, 14, 10, "connected?", fs=5.8)
    box(ax, 64, 52, 33, 12, "CONNECTED\nArduinoOTA.handle()\nleave service net after 10 min", GREEN, fs=6.3, bold=True)
    arrow(ax, (25, 56), (33, 56))
    arrow(ax, (47, 56), (64, 58), "yes", fs=6, off=(0, 2.4))
    arrow(ax, (40, 51), (44, 44), "no", fs=6, off=(-3, 0))
    arrow(ax, (55, 40), (61, 40))
    arrow(ax, (68, 45), (68, 52), "yes", fs=6, off=(3, 0))
    box(ax, 38, 20, 24, 9, "Down > 60 s and no portal in the\nlast 15 min? (or 'portal' cmd)", AMBER, fs=5.6)
    arrow(ax, (68, 35), (58, 29), "no", fs=6, off=(3, 1))
    box(ax, 72, 20, 25, 9, "Config portal (AP)\n3 min, access code", PURPLE, fs=6.3)
    arrow(ax, (62, 24.5), (72, 24.5), "yes", fs=6, off=(0, 2.4))
    box(ax, 3, 20, 25, 9, "Wait 5 s, repeat", GREY, fs=6.5)
    arrow(ax, (38, 24.5), (28, 24.5), "no", fs=6, off=(0, 2.4))
    label(ax, 50, 14.5, "escalation while the link stays down (the loop above continues meanwhile)", fs=6.5, bold=True)
    box(ax, 3, 3, 28, 8, "after 2 min:\nWiFi stack off / on", AMBER, fs=6.3)
    box(ax, 36, 3, 28, 8, "after 10 min:\nESP.restart()", RED, fs=6.3)
    box(ax, 69, 3, 28, 8, "link up again: OTA\nrestarted, counters reset", GREEN, fs=6.3)
    arrow(ax, (31, 7), (36, 7))
    save(fig, "wifi_flow")


def fig_portal_seq():
    fig, ax = canvas(4.1)
    label(ax, 50, 52.5, "Configuration portal with access code", fs=8, bold=True)
    sequence(ax, ["User (phone)", "ESP32 (AP + web page)", "Flash settings"], [
        (0, 1, "join AP 'AqsAP-WiFi' (WPA2)"),
        (0, 1, "open page 192.168.4.1"),
        (0, 1, "submit: WiFi, password, channel, key, ACCESS CODE"),
        ("self", 1, "callback keeps the form values in RAM; joins the chosen WiFi"),
        ("note", 1, "access code correct AND WiFi joined?"),
        (1, 2, "yes: commit new settings"),
        (1, 0, "yes: AP closes, unit runs on new WiFi"),
        (1, 0, "no: disconnect, NOTHING saved"),
    ], top=46, bottom=4, x0=14, x1=86)
    save(fig, "portal_seq")


def fig_flash_write():
    fig, ax = canvas(4.4)
    label(ax, 27, 57, "Writing settings", fs=8, bold=True)
    steps = ["new settings", "same as stored?  -> yes: no write", "write slot A + read back + compare",
             "write slot B + read back + compare", "both OK -> RAM copy updated"]
    y = 52
    for i, s in enumerate(steps):
        c = GREEN if i == len(steps) - 1 else (AMBER if "same" in s else BLUE)
        box(ax, 3, y - 5.2, 48, 5.2, s, c, fs=6.3)
        if i < len(steps) - 1:
            arrow(ax, (27, y - 5.2), (27, y - 7.4))
        y -= 7.4
    label(ax, 76, 57, "Boot / load", fs=8, bold=True)
    box(ax, 56, 45, 40, 6, "slot A valid? (magic, version,\nsize, CRC32)", AMBER, fs=6)
    box(ax, 56, 34, 40, 6, "yes: use A (repair B if damaged)", GREEN, fs=6)
    box(ax, 56, 24, 40, 6, "no: slot B valid? -> use B, repair A", BLUE, fs=6)
    box(ax, 56, 13, 40, 6, "no: factory defaults (i2_secrets.h)", RED, fs=6)
    arrow(ax, (76, 45), (76, 40))
    arrow(ax, (76, 34), (76, 30), "")
    arrow(ax, (76, 24), (76, 19), "")
    save(fig, "flash_write")


def fig_calibration_seq():
    fig, ax = canvas(3.5)
    label(ax, 50, 45.5, "Remote calibration with confirmation", fs=8, bold=True)
    sequence(ax, ["Administrator", "ThingSpeak\ncalibration channel", "Sensor unit", "Operator (serial)"], [
        (0, 1, "write field 1: 3,1.05,-1.2,1.02,0,1,-0.5,1,2"),
        (2, 1, "hourly read (or 'cal fetch')"),
        ("self", 2, "version 3 > local 2  ->  PENDING"),
        (2, 3, "[cal] NEW calibration version 3"),
        (3, 2, "cal  /  login <code>  /  cal accept"),
        ("self", 2, "stored in flash, used from the next sample"),
    ], top=39, bottom=3, x0=14, x1=86)
    save(fig, "calibration_seq")


def fig_matrix_flow():
    fig, ax = canvas(3.4)
    box(ax, 2, 22, 18, 11, "ThingSpeak\nchannel", PURPLE, fs=7, bold=True)
    box(ax, 26, 22, 20, 11, "ts_read task\n(every 30 s)\nreadMultipleFields", BLUE, fs=6.5)
    box(ax, 52, 24, 12, 7, "queue\nlength 1", PURPLE, fs=6.3, ls="--")
    box(ax, 70, 22, 20, 11, "display task\n(scroll step 60 ms)", BLUE, fs=6.5)
    arrow(ax, (20, 27.5), (26, 27.5), "HTTP", fs=6, off=(0, 2.4))
    arrow(ax, (46, 27.5), (52, 27.5), "overwrite", fs=5.5, off=(0, 2.4))
    arrow(ax, (64, 27.5), (70, 27.5), "newest", fs=5.5, off=(0, 2.4))
    box(ax, 70, 6, 20, 8, "NeoPixel matrix", GREEN, fs=6.8)
    arrow(ax, (80, 22), (80, 14))
    box(ax, 26, 6, 32, 8, "wifiTask: i2wifiConnected()\n(shows 'Connecting..' when down)", GREY, fs=5.8)
    arrow(ax, (58, 10), (70, 25), "", ls="--")
    box(ax, 26, 38, 32, 7, "no good read for 30 min  ->  ESP.restart()", RED, fs=6)
    arrow(ax, (36, 33), (36, 38), "", ls="--")
    save(fig, "matrix_flow")


def fig_versions():
    fig, ax = canvas(3.6)
    items = [
        ("v0  (schools)", "Super-loop\nPMS + display + TS\nfixed WiFi, no OTA", GREY),
        ("v1  Sep-Oct", "RTOS prototype\ntasks + queue, OLED,\nThingSpeak, OTA", BLUE),
        ("v2  Oct 28", "FSM, WiFiManager,\nEEPROM, matrix unit,\nfirst microphone test", BLUE),
        ("v3  now", "I2Common library,\nflash settings, supervisor,\nportal + code, calibration,\nSLM, perf tools", GREEN),
    ]
    for i, (t, d, c) in enumerate(items):
        x = 2 + i * 24.5
        box(ax, x, 12, 22, 22, "", c)
        label(ax, x + 11, 30, t, fs=6.8, bold=True)
        label(ax, x + 11, 20, d, fs=5.8)
        if i < 3:
            arrow(ax, (x + 22, 23), (x + 24.5, 23))
    label(ax, 50, 5, "Development history (approximate - confirm dates and contents)", fs=6.5, col="#6b7280", style="italic")
    save(fig, "versions")


# ----------------------------------------------------------------------------
#  REPORT 2  -  sound level meter / DSP figures
# ----------------------------------------------------------------------------
FS = 48000.0
MIC_EQ = (1.00197834654696, [(-1.986920458344451, 0.986963226946616, 1.995178510504166, -0.995184322194091)])
A_W = (0.169994948147430, [
    (-2.00026996133106, 1.00027056142719, -1.060868438509278, -0.163987445885926),
    (4.35912384203144, 3.09120265783884, 1.208419926363593, -0.273166998428332),
    (-0.70930303489759, -0.29071868393580, 1.982242159753048, -0.982298594928989)])


def sos_response(sos, f, fs=FS):
    gain, sections = sos
    w = 2 * np.pi * f / fs
    z1 = np.exp(-1j * w)
    z2 = z1 * z1
    h = gain * np.ones_like(z1)
    for b1, b2, a1, a2 in sections:                    # same convention as sos-iir-filter.h
        h = h * (1 + b1 * z1 + b2 * z2) / (1 - a1 * z1 - a2 * z2)
    return h


def a_weight_analytic(f):
    f2 = f ** 2
    ra = (12194.0 ** 2 * f2 ** 2) / ((f2 + 20.6 ** 2) * np.sqrt((f2 + 107.7 ** 2) * (f2 + 737.9 ** 2)) * (f2 + 12194.0 ** 2))
    return 20 * np.log10(ra) + 2.0


def fig_slm_chain():
    fig, ax = canvas(4.2)
    stages = [
        ("INMP441\nMEMS mic\nPDM -> I2S", GREEN, 2),
        ("I2S + DMA\n48 kHz\n32-bit slots", GREY, 15),
        ("int24 -> float\n(shift 8 bits)", BLUE, 28),
        ("Equalizer IIR\n(microphone\nresponse)", BLUE, 41),
        ("A-weighting\nIIR\n(3 sections)", BLUE, 54),
        ("sum of squares\nper 125 ms\nblock", BLUE, 67),
        ("dB, 1 s Leq,\nflags", PURPLE, 80),
    ]
    for t, c, x in stages:
        box(ax, x, 32, 12, 14, t, c, fs=5.8)
    for i in range(len(stages) - 1):
        arrow(ax, (stages[i][2] + 12, 39), (stages[i + 1][2], 39))
    box(ax, 80, 12, 18, 11, "energy average\n+ max level\nper upload window", PURPLE, fs=5.8)
    arrow(ax, (86, 32), (86, 23))
    box(ax, 41, 12, 28, 11, "ThingSpeak\nfield 5: LAeq   field 6: LAmax", GREEN, fs=6)
    arrow(ax, (80, 17.5), (69, 17.5))
    box(ax, 2, 12, 30, 11, "on request: FFT of 512 raw samples\n(spectrum for diagnostics / tones)", AMBER, fs=5.8)
    arrow(ax, (34, 32), (22, 23), "", ls="--")
    label(ax, 50, 5, "slm_reader task (high priority)  ->  queue  ->  slm_calc task (low priority)", fs=6.5, col="#6b7280")
    ax.plot([29, 78.5], [49, 49], color="#6d28d9", lw=1.5)
    label(ax, 54, 52, "slm_reader task (prio 4, core 1)", fs=6.2, col="#6d28d9")
    save(fig, "slm_chain")


def fig_i2s_timing():
    fig = plt.figure(figsize=(7.5, 3.3), dpi=200)
    ax = fig.add_axes([0.1, 0.1, 0.88, 0.85])
    t = np.arange(0, 20, 0.01)
    bclk = ((t % 1) < 0.5).astype(float)
    ws = np.where((t >= 2) & (t < 12), 0.0, 1.0)          # left channel = WS low for 10 clocks (schematic)
    bits = [1, 0, 1, 1, 0, 0, 1, 0]
    sd = np.zeros_like(t)
    for i, b in enumerate(bits):
        sd[(t >= 3 + i) & (t < 4 + i)] = b
    sd[(t >= 11) & (t < 20)] = 0
    for y0, sig, name in ((4.4, bclk, "BCLK (SCK)"), (2.2, ws, "WS (word select)"), (0, sd, "SD (data)")):
        ax.step(t, sig * 1.2 + y0, where="post", color="#1d4ed8", lw=1.2)
        ax.text(-0.4, y0 + 0.6, name, ha="right", va="center", fontsize=7)
    ax.axvline(2, color="#9ca3af", ls=":", lw=0.8)
    ax.axvline(3, color="#9ca3af", ls=":", lw=0.8)
    ax.annotate("WS edge", xy=(2, 3.4), xytext=(0.2, 6.2), fontsize=6.5, arrowprops=dict(arrowstyle="-", lw=0.6))
    ax.annotate("MSB appears one BCLK\nafter the WS edge (I2S)", xy=(3.5, 1.2), xytext=(5, 6.0), fontsize=6.5,
                arrowprops=dict(arrowstyle="-", lw=0.6))
    for i, b in enumerate(bits):
        ax.text(3.5 + i, 0.6, f"b{23 - i}", ha="center", va="center", fontsize=5.5, color="#b45309")
    ax.text(15.5, 0.6, "...", fontsize=8, ha="center")
    ax.text(16, 3.0, "left channel slot = 32 BCLK:\n24 data bits + 8 padding bits", fontsize=6.5, ha="center")
    ax.set_xlim(-0.2, 20)
    ax.set_ylim(-0.5, 7)
    ax.axis("off")
    fig.savefig(os.path.join(OUT, "i2s_timing.png"), dpi=200, facecolor="white")
    plt.close(fig)




def fig_biquad():
    fig, ax = canvas(3.9)
    label(ax, 50, 49.5, "One second-order section (biquad), Direct Form II - as in sos-iir-filter.h", fs=7.5, bold=True)

    def line(pts, arrow_end=False, col="#374151"):
        xs = [p[0] for p in pts]
        ys = [p[1] for p in pts]
        if arrow_end:
            ax.plot(xs[:-1], ys[:-1], color=col, lw=1.1, zorder=4)
            arrow(ax, pts[-2], pts[-1], col=col)
        else:
            ax.plot(xs, ys, color=col, lw=1.1, zorder=4)

    def adder(x, y):
        ax.add_patch(plt.Circle((x, y), 2.2, fc="#fef3c7", ec="#b45309", zorder=3))
        label(ax, x, y, "+", fs=9)

    def node(x, y):
        ax.plot([x], [y], "o", color="k", ms=3.5, zorder=6)

    y0 = 25
    box(ax, 1, y0 - 3, 7, 6, "x[n]", WHITE, fs=7)
    adder(14, y0)
    line([(8, y0), (11.8, y0)], True)
    line([(16.2, y0), (24, y0)])
    node(24, y0)
    label(ax, 24, y0 + 3, "t[n]", fs=6.3)
    box(ax, 28, y0 - 3, 9, 6, "z^-1", PURPLE, fs=7)
    line([(24, y0), (28, y0)], True)
    line([(37, y0), (42, y0)])
    node(42, y0)
    label(ax, 42, y0 + 3, "w0", fs=6.3)
    box(ax, 46, y0 - 3, 9, 6, "z^-1", PURPLE, fs=7)
    line([(42, y0), (46, y0)], True)
    line([(55, y0), (60, y0)])
    node(60, y0)
    label(ax, 62.5, y0 + 3, "w1", fs=6.3)
    # feedback (above)
    box(ax, 38.5, 31, 7, 4, "a1", GREEN, fs=7)
    line([(42, y0), (42, 31)])
    line([(38.5, 33), (14, 33)])
    line([(14, 33), (14, y0 + 2.2)], True)
    box(ax, 56.5, 37, 7, 4, "a2", GREEN, fs=7)
    line([(60, y0), (60, 37)])
    line([(56.5, 39), (14, 39), (14, 33)])
    # feed-forward (below) and output adder
    adder(82, y0)
    box(ax, 88, y0 - 3, 9, 6, "y[n]", WHITE, fs=7)
    line([(84.2, y0), (88, y0)], True)
    box(ax, 38.5, 9, 7, 4, "b1", GREEN, fs=7)
    line([(42, y0), (42, 13)])
    line([(45.5, 11), (78, 11)])
    box(ax, 56.5, 15, 7, 4, "b2", GREEN, fs=7)
    line([(60, y0), (60, 19)])
    line([(63.5, 17), (78, 17)])
    line([(24, y0), (24, 5), (78, 5)])
    label(ax, 51, 6.9, "b0 = 1 (direct path)", fs=5.8)
    line([(78, 5), (78, y0), (79.8, y0)], True)
    label(ax, 50, 1.2, "t[n] = x[n] + a1*w0 + a2*w1      y[n] = t[n] + b1*w0 + b2*w1      (overall gain is applied in the last section)", fs=6)
    save(fig, "biquad")


def fig_filter_responses():
    f = np.logspace(np.log10(10), np.log10(20000), 600)
    ha = 20 * np.log10(np.abs(sos_response(A_W, f)))
    he = 20 * np.log10(np.abs(sos_response(MIC_EQ, f)))
    ref = a_weight_analytic(f)
    fig, axs = plt.subplots(1, 2, figsize=(7.5, 3.1), dpi=200)
    axs[0].semilogx(f, ref, color="#9ca3af", lw=2.4, label="IEC A-weighting (analytic)")
    axs[0].semilogx(f, ha, color="#1d4ed8", lw=1.2, ls="--", label="IIR from the project (Fs 48 kHz)")
    axs[0].set_ylim(-60, 8)
    axs[0].set_title("A-weighting filter", fontsize=8)
    axs[1].semilogx(f, he, color="#15803d", lw=1.4, label="INMP441 equalizer (1 section)")
    axs[1].set_ylim(-3, 30)
    axs[1].set_title("Microphone equalizer", fontsize=8)
    for a in axs:
        a.grid(True, which="both", alpha=0.3)
        a.set_xlabel("frequency [Hz]", fontsize=7)
        a.set_ylabel("gain [dB]", fontsize=7)
        a.tick_params(labelsize=6.5)
        a.legend(fontsize=6, loc="lower right" if a is axs[0] else "upper right")
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "filter_responses.png"), dpi=200, facecolor="white")
    plt.close(fig)
    i1k = np.argmin(np.abs(f - 1000))
    print("A-weighting IIR @1 kHz: %.2f dB (analytic %.2f dB); max deviation 20 Hz..16 kHz: %.2f dB" % (
        ha[i1k], ref[i1k], np.max(np.abs((ha - ref)[(f > 20) & (f < 16000)]))))
    print("Mic equalizer @100 Hz: %.2f dB, @1 kHz: %.3f dB" % (he[np.argmin(np.abs(f - 100))], he[i1k]))
    return ha, ref


def fig_fft_demo():
    fs, n = 48000.0, 512
    t = np.arange(n) / fs
    x = 1.0 * np.sin(2 * np.pi * 440 * t) + 0.5 * np.sin(2 * np.pi * 1000 * t) + 0.25 * np.sin(2 * np.pi * 3000 * t)
    w = np.hamming(n)
    spec = np.abs(np.fft.rfft(x * w)) / (np.sum(w) / 2)
    freqs = np.fft.rfftfreq(n, 1 / fs)
    fig, axs = plt.subplots(1, 2, figsize=(7.5, 2.9), dpi=200)
    axs[0].plot(t * 1000, x, color="#1d4ed8", lw=0.9)
    axs[0].set_title("512 samples = 10.67 ms (3 tones)", fontsize=8)
    axs[0].set_xlabel("time [ms]", fontsize=7)
    axs[0].set_ylabel("amplitude", fontsize=7)
    axs[1].plot(freqs, spec, color="#b45309", lw=0.9)
    axs[1].set_xlim(0, 5000)
    axs[1].set_title("Hamming-windowed FFT (bin = 93.75 Hz)", fontsize=8)
    axs[1].set_xlabel("frequency [Hz]", fontsize=7)
    axs[1].set_ylabel("magnitude", fontsize=7)
    for a in axs:
        a.grid(alpha=0.3)
        a.tick_params(labelsize=6.5)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "fft_demo.png"), dpi=200, facecolor="white")
    plt.close(fig)


def fig_window_leakage():
    fs, n = 48000.0, 512
    t = np.arange(n) / fs
    x = np.sin(2 * np.pi * 1000.0 * t)                       # 1000 Hz is not on a bin (10.67 bins)
    fig, ax = plt.subplots(figsize=(7.5, 2.9), dpi=200)
    freqs = np.fft.rfftfreq(n, 1 / fs)
    for name, w, col in (("rectangular (no window)", np.ones(n), "#b91c1c"), ("Hamming", np.hamming(n), "#1d4ed8")):
        s = np.abs(np.fft.rfft(x * w)) / (np.sum(w) / 2)
        ax.plot(freqs, 20 * np.log10(s + 1e-12), color=col, lw=1.0, label=name)
    ax.set_xlim(0, 12000)
    ax.set_ylim(-100, 5)
    ax.set_xlabel("frequency [Hz]", fontsize=7)
    ax.set_ylabel("magnitude [dB re 1]", fontsize=7)
    ax.set_title("Spectral leakage of a 1 kHz tone that does not fall on a bin", fontsize=8)
    ax.legend(fontsize=6.5)
    ax.grid(alpha=0.3)
    ax.tick_params(labelsize=6.5)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "window_leakage.png"), dpi=200, facecolor="white")
    plt.close(fig)


def fig_slm_tasks():
    fig, ax = canvas(3.6)
    box(ax, 2, 22, 18, 11, "INMP441", GREEN, fs=7, bold=True)
    box(ax, 25, 22, 16, 11, "I2S DMA\n32 banks x 375\n= 250 ms", GREY, fs=6)
    box(ax, 46, 22, 20, 11, "slm_reader\nprio 4, core 1\nblocks on i2s_read", BLUE, fs=6.3, bold=True)
    box(ax, 71, 24, 8, 7, "queue\n(8)", PURPLE, fs=6, ls="--")
    box(ax, 83, 22, 15, 11, "slm_calc\nprio 2, core 1", BLUE, fs=6.3, bold=True)
    arrow(ax, (20, 27.5), (25, 27.5), "", both=False)
    arrow(ax, (41, 27.5), (46, 27.5))
    arrow(ax, (66, 27.5), (71, 27.5), "2 sums", fs=5.5, off=(0, 2.6))
    arrow(ax, (79, 27.5), (83, 27.5))
    box(ax, 60, 6, 38, 8, "mutex-protected window\n(energy sum, count, max)", PURPLE, fs=6)
    arrow(ax, (90, 22), (90, 14))
    box(ax, 2, 6, 40, 8, "pms_fsm: slmTakeWindow() at the end of\neach MEASURE window -> ThingSpeak", BLUE, fs=6)
    arrow(ax, (60, 10), (42, 10), "", both=True)
    box(ax, 2, 38, 96, 5, "Rule: the reader must finish each block before the next one is complete (budget 125 ms). If it does not, DMA buffers overflow.", AMBER, fs=6.3)
    save(fig, "slm_tasks")


def fig_timing_budget():
    fig, ax = canvas(3.2)
    label(ax, 50, 40, "Real-time budget of the sound-level pipeline", fs=8, bold=True)
    for i in range(4):
        x = 4 + i * 23
        box(ax, x, 22, 23, 8, f"I2S fills block {i+1}\n(125 ms = 6000 samples)", GREY, fs=5.6, rounding=0.3)
        if i > 0:
            box(ax, x, 12, 7, 6, "C", BLUE, fs=7, rounding=0.3)
            label(ax, x + 3.5, 9.3, "filter\n(C)", fs=5.2)
    ax.plot([4 + 23, 4 + 23], [20, 31], color="#b91c1c", lw=0.8, ls=":")
    label(ax, 50, 3.5, "Deadline: C < T = 125 ms.  CPU utilisation of this task  U = C / T.  Measured with the 'slm_block' probe.", fs=6.2)
    arrow(ax, (27, 15), (50, 15), "T = 125 ms", fs=5.8, both=True, off=(0, 2.2))
    save(fig, "timing_budget")


def fig_level_ranges():
    fig, ax = plt.subplots(figsize=(7.5, 3.0), dpi=200)
    spl = np.linspace(0, 130, 200)
    dbfs = spl - 94 - 26 + 26 + 0                         # L_SPL = 94 + (dBFS + 26)  ->  dBFS = L_SPL - 120
    dbfs = spl - 120
    ax.plot(spl, dbfs, color="#1d4ed8", lw=1.6)
    for x, y, txt, c in ((33, 33 - 120, "noise floor\n33 dB SPL", "#b45309"), (94, 94 - 120, "94 dB SPL\n= -26 dBFS (reference)", "#15803d"),
                         (120, 0, "acoustic overload\n120 dB SPL = 0 dBFS", "#b91c1c")):
        ax.plot([x], [y], "o", color=c, ms=5)
        ax.annotate(txt, xy=(x, y), xytext=(x - 28 if x > 100 else x + 4, y + (-22 if x < 100 else -35)), fontsize=6.3, color=c,
                    arrowprops=dict(arrowstyle="-", color=c, lw=0.6))
    ax.axvspan(33, 120, color="#dbeafe", alpha=0.5)
    ax.text(76, -110, "usable range (about 87 dB)", fontsize=6.5, ha="center", color="#1d4ed8")
    ax.set_xlabel("sound pressure level [dB SPL]", fontsize=7)
    ax.set_ylabel("digital level [dBFS]", fontsize=7)
    ax.set_title("Mapping of acoustic level to digital level (INMP441 datasheet values used in the code)", fontsize=7.5)
    ax.grid(alpha=0.3)
    ax.tick_params(labelsize=6.5)
    ax.set_ylim(-125, 10)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "level_ranges.png"), dpi=200, facecolor="white")
    plt.close(fig)


def main():
    for fn in (fig_system_overview, fig_sensor_hw, fig_matrix_hw, fig_superloop_vs_rtos, fig_task_states, fig_queue,
               fig_priority_inversion, fig_layers, fig_sensor_tasks, fig_fsm, fig_wifi_flow, fig_portal_seq,
               fig_flash_write, fig_calibration_seq, fig_matrix_flow, fig_versions, fig_slm_chain, fig_i2s_timing,
               fig_biquad, fig_filter_responses, fig_fft_demo, fig_window_leakage, fig_slm_tasks, fig_timing_budget,
               fig_level_ranges):
        fn()
    print("figures written to", os.path.abspath(OUT))


if __name__ == "__main__":
    main()
