"""Extra figure for report 2: A-weighting designed with the plain bilinear transform vs. the project's IIR (direct digital fit).

Run:  python diagrams_report2_extra.py   (writes ../figures/a_weight_methods.png)
"""
import os
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import diagrams as d

FS = 48000.0
F1, F2, F3, F4 = 20.598997, 107.65265, 737.86223, 12194.217   # IEC 61672 pole frequencies [Hz]


def w(f):
    return 2 * np.pi * f


def analog(f):
    s = 1j * w(f)
    return s ** 4 / ((s + w(F1)) ** 2 * (s + w(F2)) * (s + w(F3)) * (s + w(F4)) ** 2)


def bilinear(f):
    z = np.exp(1j * 2 * np.pi * f / FS)
    s = 2 * FS * (z - 1) / (z + 1)                          # no pre-warping
    return s ** 4 / ((s + w(F1)) ** 2 * (s + w(F2)) * (s + w(F3)) * (s + w(F4)) ** 2)


def db(x):
    return 20 * np.log10(np.abs(x))


f = np.logspace(np.log10(10), np.log10(20000), 800)
f1k = np.array([1000.0])
ana = db(analog(f)) - db(analog(f1k))[0]
bil = db(bilinear(f)) - db(bilinear(f1k))[0]
prj = db(d.sos_response(d.A_W, f))

fig, axs = plt.subplots(1, 2, figsize=(7.5, 3.1), dpi=200)
axs[0].semilogx(f, ana, color="#9ca3af", lw=2.4, label="analytic (IEC 61672)")
axs[0].semilogx(f, bil, color="#b91c1c", lw=1.1, label="bilinear transform, no pre-warping")
axs[0].semilogx(f, prj, color="#1d4ed8", lw=1.1, ls="--", label="project IIR (direct digital fit)")
axs[0].set_ylim(-60, 8)
axs[0].set_title("A-weighting, Fs = 48 kHz", fontsize=8)
axs[0].set_ylabel("gain [dB]", fontsize=7)
axs[0].legend(fontsize=5.8, loc="lower right")
axs[1].semilogx(f, bil - ana, color="#b91c1c", lw=1.2, label="bilinear - analytic")
axs[1].semilogx(f, prj - ana, color="#1d4ed8", lw=1.2, label="project IIR - analytic")
axs[1].set_ylim(-20, 3)
axs[1].set_title("Error against the analytic curve", fontsize=8)
axs[1].set_ylabel("error [dB]", fontsize=7)
axs[1].legend(fontsize=6, loc="lower left")
for a in axs:
    a.grid(True, which="both", alpha=0.3)
    a.set_xlabel("frequency [Hz]", fontsize=7)
    a.tick_params(labelsize=6.5)
fig.tight_layout()
out = os.path.join(d.OUT, "a_weight_methods.png")
fig.savefig(out, dpi=200, facecolor="white")
print("saved", os.path.abspath(out))
