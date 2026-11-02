#!/usr/bin/env python3
# Sanctuary – SPDX-License-Identifier: GPL-3.0-or-later
# Objective checks for rendered WAVs (we cannot listen in CI):
#   analyze.py stats <file.wav> [--xfade t0,t1] [--silent t0,t1]
#       peak, RMS, DC, energy above 12 / 16 kHz, stereo width, click detector,
#       crossfade loudness dip (--xfade), level in a window that must be silent (--silent)
#   analyze.py alias <file.wav> <f0> [--from t0 --to t1]
#       energy that is not a harmonic of f0 (aliasing / interpolation images), relative to total
#   analyze.py levels <file.wav> [--step 0.5]        RMS curve over time
import sys
import numpy as np
from scipy import signal, linalg


def read_wav(path):
    raw = open(path, "rb").read()
    assert raw[:4] == b"RIFF" and raw[8:12] == b"WAVE"
    pos, fmt, data = 12, None, None
    while pos < len(raw):
        cid, size = raw[pos:pos + 4], int.from_bytes(raw[pos + 4:pos + 8], "little")
        body = raw[pos + 8:pos + 8 + size]
        if cid == b"fmt ":
            fmt = (int.from_bytes(body[0:2], "little"), int.from_bytes(body[2:4], "little"),
                   int.from_bytes(body[4:8], "little"), int.from_bytes(body[14:16], "little"))
        elif cid == b"data":
            data = body
        pos += 8 + size + (size & 1)
    tag, ch, sr, bits = fmt
    if tag == 3 and bits == 32:
        x = np.frombuffer(data, dtype="<f4")
    elif tag == 1 and bits == 16:
        x = np.frombuffer(data, dtype="<i2") / 32768.0
    else:
        raise SystemExit("unsupported wav format")
    return x.reshape(-1, ch).astype(np.float64), sr


def db(x):
    return 20 * np.log10(np.maximum(x, 1e-12))


def arg(name, default=None):
    if name in sys.argv:
        return sys.argv[sys.argv.index(name) + 1]
    return default


def level_curve(m, sr, win=0.05, smooth=0.3):
    hop = int(win * sr)
    n = len(m) // hop
    e = (m[: n * hop].reshape(n, hop) ** 2).mean(axis=1)
    k = max(1, int(smooth / win))
    e = np.convolve(e, np.ones(k) / k, mode="same")
    return np.arange(n) * win + win / 2, 10 * np.log10(np.maximum(e, 1e-24))


def _lpc(block, order):
    r = np.correlate(block, block, "full")[len(block) - 1:len(block) + order]
    r[0] *= 1.0 + 1e-9
    a = linalg.solve_toeplitz(r[:order], -r[1:order + 1])
    return np.concatenate(([1.0], a))


def clicks(x, sr):
    """Click detector: linear-prediction residual: per 4096-sample block an order-24 predictor is fitted; the pad,
       its noise and the reverb are predictable or Gaussian, a discontinuity is not. Reported is the
       largest |residual| / local RMS of the residual (~40 ms), robust against fades and swells.
       Calibration (injected into clean renders): clean 6-9.5; a -40 dBFS step 20-27; a -50 dBFS
       one-sample spike 14-16. Values above ~13 mean a click.
    Returns (lpc_ratio, at_s, largest sample step, size of that residual spike)."""
    m = x.mean(axis=1)
    worst, at, absval = 0.0, 0.0, 0.0
    B, P = 4096, 24
    for s0 in range(0, len(m) - B, B // 2):
        blk = m[s0:s0 + B]
        if np.abs(blk).max() < 1e-4:               # (near) silence: nothing to hear
            continue
        a = _lpc(blk * np.hanning(B), P)
        e = signal.lfilter(a, [1.0], blk)[P:]
        env = np.convolve(np.abs(blk), np.ones(256) / 256, mode="same")[P:]
        live = env > 0.05 * env.max()              # ignore the silent part of a fade-in block
        if live.sum() < B // 4:
            continue
        k = np.ones(2048); k[1024 - 32:1024 + 32] = 0; k /= k.sum()     # local level, the point itself excluded
        loc = np.sqrt(np.convolve(e * e, k, mode="same")) + 1e-12
        r = np.where(live, np.abs(e) / loc, 0.0)
        i = int(np.argmax(r))
        if r[i] > worst:
            worst, at, absval = r[i], (s0 + P + i) / sr, abs(e[i])
    d = np.abs(np.diff(x, axis=0)).max(axis=1)
    return worst, at, d.max(), absval


def stats(path):
    x, sr = read_wav(path)
    peak = np.abs(x).max()
    rms = np.sqrt((x ** 2).mean())
    dc = np.abs(x.mean(axis=0)).max()
    f, p = signal.welch(x, sr, nperseg=8192, axis=0)
    p = p.sum(axis=1)
    tot = p.sum()
    hf12 = p[f > 12000].sum() / tot
    hf16 = p[f > 16000].sum() / tot
    L, R = x[:, 0], x[:, 1]
    mid, side = 0.5 * (L + R), 0.5 * (L - R)
    corr = np.corrcoef(L, R)[0, 1] if rms > 0 else 1.0
    sm = 10 * np.log10((side ** 2).sum() / max((mid ** 2).sum(), 1e-20))
    cl, cat, step, cabs = clicks(x, sr)
    print(f"{path}")
    print(f"  Dauer {len(x)/sr:.1f} s   Spitze {db(peak):.2f} dBFS   RMS {db(rms):.1f} dBFS   DC {db(dc):.0f} dBFS")
    print(f"  Energie > 12 kHz {10*np.log10(hf12+1e-20):.0f} dB, > 16 kHz {10*np.log10(hf16+1e-20):.0f} dB (relativ zur Gesamtenergie)")
    print(f"  Stereo: Korrelation L/R {corr:.2f}, Seite/Mitte {sm:.1f} dB")
    print(f"  Klick-Detektor (LPC-Residuum / lokaler Pegel; sauber 6-9.5, Klick > 13): {cl:.1f} (bei {cat:.2f} s, Residuum {db(cabs):.0f} dBFS); größter Sample-Schritt {step:.4f}")
    # 1/3-octave-ish smoothness: largest jump between neighbouring bands 100 Hz .. 10 kHz
    edges = 100 * 2 ** (np.arange(0, 21) / 3)
    bands = [p[(f >= a) & (f < b)].sum() for a, b in zip(edges[:-1], edges[1:])]
    bdb = 10 * np.log10(np.array(bands) + 1e-30)
    print(f"  Spektrum (Terzbänder 100 Hz–10 kHz): größter Sprung zwischen Nachbarbändern {np.abs(np.diff(bdb)).max():.1f} dB")
    xf = arg("--xfade")
    if xf:
        t0, t1 = map(float, xf.split(","))
        t, lv = level_curve(mid + 0 * side, sr)
        t2, lv2 = level_curve(np.sqrt(0.5 * (L ** 2 + R ** 2)) * np.sign(L + 1e-30), sr)
        before = np.median(lv2[(t > t0 - 3) & (t < t0)])
        after = np.median(lv2[(t > t1 + 1) & (t < t1 + 4)])
        during = lv2[(t >= t0) & (t <= t1 + 1)]
        ref = min(before, after)
        print(f"  Überblendung {t0:.1f}–{t1:.1f} s: vorher {before:.1f} dB, nachher {after:.1f} dB, "
              f"Minimum während {during.min():.1f} dB -> Einbruch {min(0.0, during.min()-ref):.1f} dB, "
              f"Maximum während {during.max():.1f} dB (Überhöhung {max(0.0, during.max()-max(before, after)):.1f} dB)")
    si = arg("--silent")
    if si:
        a, b = map(float, si.split(","))
        seg = x[int(a * sr):int(b * sr)]
        print(f"  Stille {a}–{b} s: Spitze {db(np.abs(seg).max()):.0f} dBFS")


def alias(path, f0):
    x, sr = read_wav(path)
    a, b = float(arg("--from", 1.0)), float(arg("--to", 3.0))
    m = x[int(a * sr):int(b * sr)].mean(axis=1)
    w = signal.windows.blackmanharris(len(m))
    S = np.abs(np.fft.rfft(m * w)) ** 2
    f = np.fft.rfftfreq(len(m), 1 / sr)
    h = f / f0
    near = np.abs(h - np.round(h)) * f0 < 25 + 0.004 * f      # within 25 Hz (+ drift) of a harmonic
    near &= np.round(h) >= 1
    tot = S[f > 20].sum()
    bad = S[(~near) & (f > 20)].sum()
    bad12 = S[(~near) & (f > 12000)].sum()
    print(f"{path}: f0 {f0:.1f} Hz – nicht-harmonische Energie {10*np.log10(bad/tot+1e-20):.0f} dB, "
          f"davon über 12 kHz {10*np.log10(bad12/tot+1e-20):.0f} dB, harmonische Energie über 12 kHz "
          f"{10*np.log10(S[near & (f > 12000)].sum()/tot+1e-20):.0f} dB (relativ zur Gesamtenergie)")


def levels(path):
    x, sr = read_wav(path)
    step = float(arg("--step", 0.5))
    t, lv = level_curve(np.sqrt(0.5 * (x[:, 0] ** 2 + x[:, 1] ** 2)), sr, win=0.05, smooth=0.05)
    k = int(step / 0.05)
    print(" ".join(f"{tt:.1f}:{l:.0f}" for tt, l in zip(t[::k], lv[::k])))


if __name__ == "__main__":
    cmd = sys.argv[1]
    if cmd == "stats":
        stats(sys.argv[2])
    elif cmd == "alias":
        alias(sys.argv[2], float(sys.argv[3]))
    elif cmd == "levels":
        levels(sys.argv[2])
