#!/usr/bin/env python3
"""Independent mathematical measurements for generated waveform files.

The on-disk layout follows docs/09-output-format.md.  This tool never reads
or imports the generator sources; it uses only the Python standard library.

Tolerances are mathematical reconstruction tolerances (relative RMS, mHz,
degrees, sample-grid timing), not IEC measurement limits.  Dynamic scenarios
(frequency/amplitude ramps) are judged on trajectory and structure only:
no steady-state phase verdict is produced.

Generic measurements (all print JSON):
    check_acceptance.py rms  FILE [--channel Ua] [--window 0.2] [--start 0]
    check_acceptance.py freq FILE [--channel Ua] [--window 1] [--start 0] [--end N]
    check_acceptance.py dft  FILE [--channel Ua] [--freq 50] [--start 0] [--end N]

Acceptance cases:
    check_acceptance.py a1-1-1 FILE    A1.1.1 frequency triangle (42.5..57.5 Hz)
    check_acceptance.py a2-5-1 FILE    A2.5.1 amplitude triangle (10..80 %, 50.125 Hz)
    check_acceptance.py a4-2-1 FILE    A4.2.1 sequential three-phase 0 % dip
    check_acceptance.py a5-1-2 FILE    A5.1.2 unbalanced three-phase RMS
    check_acceptance.py a6-2-2 FILE    A6.2.2 3rd harmonic 10 %
    check_acceptance.py a6-5-1 FILE    A6.5.1 H3 amplitude ramp (trajectory only)
    check_acceptance.py a8-1-3 FILE    A8.1.3 1060 Hz carrier 3 %

Each case prints a JSON object with "checks" (one row per judgement) and
"values" (the raw measurements used by report.md).  Exit status 0 = PASS,
1 = FAIL.
"""

import argparse
import json
import math
import os
import struct
import sys
from array import array

FILE_MAGIC = b"WGIEC625"
FRAME_MAGIC = b"WGF1"
END_MAGIC = b"WGE1"

FILE_HDR = struct.Struct("<8sHHIHHHHQdddQ")   # 64 bytes, docs/09 section 2
FRAME_HDR = struct.Struct("<4sHHQQQQIIHHI")   # 56 bytes, docs/09 section 3
END_REC = struct.Struct("<4sHHQQQ")           # 32 bytes, docs/09 section 4

CHANNEL_NAMES = ["Ua", "Ub", "Uc", "Ia", "Ib", "Ic", "In"]
BYTES_PER_SAMPLE = {0: 8, 1: 4, 2: 3}
TWO_PI = 2.0 * math.pi

# Mathematical reconstruction tolerances (not IEC limits).
MATH_RMS_REL = 0.001       # steady RMS relative error <= 0.1 %
MATH_FREQ_HZ = 0.001       # steady frequency error <= 1 mHz
MATH_HARM_REL = 0.005      # harmonic / carrier RMS relative error <= 0.5 %
TRAJ_FREQ_HZ = 0.1         # 1 s window mean vs analytic triangle mean
TRAJ_AMP_FLOOR_PCT = 0.05  # absolute floor near a zero-amplitude ramp point


# --------------------------------------------------------------------------
# formatting helpers
# --------------------------------------------------------------------------

def rr(x, nd=6):
    if x is None:
        return None
    v = float(x)
    if not math.isfinite(v):
        return None
    return round(v, nd)


class Checks(object):
    def __init__(self):
        self.items = []

    def ok(self, cid, cond, expected, actual, tol=None, unit=None, note=None):
        item = {
            "id": cid,
            "expected": expected,
            "actual": actual,
            "tolerance": tol,
            "unit": unit,
            "status": "PASS" if cond else "FAIL",
        }
        if note:
            item["note"] = note
        self.items.append(item)
        return bool(cond)

    def status(self):
        return "FAIL" if any(c["status"] == "FAIL" for c in self.items) else "PASS"


# --------------------------------------------------------------------------
# wave file reader (docs/09 only)
# --------------------------------------------------------------------------

def decode_plane(raw, frames, fmt, scale):
    if fmt == 0:
        vals = array("d")
        vals.frombytes(raw)
        if sys.byteorder != "little":
            vals.byteswap()
        return vals
    if fmt == 1:
        vals = array("f")
        vals.frombytes(raw)
        if sys.byteorder != "little":
            vals.byteswap()
        return array("d", vals)
    out = array("d")
    for i in range(frames):
        j = 3 * i
        v = raw[j] | (raw[j + 1] << 8) | (raw[j + 2] << 16)
        if v & 0x800000:
            v -= 0x1000000
        out.append(v * scale)
    return out


class Waveform(object):
    """Decoded waveform: one array('d') per requested channel."""

    def __init__(self, path, channels=None):
        self.path = os.path.abspath(path)
        self.size = os.path.getsize(path)
        with open(path, "rb") as fh:
            raw = fh.read(FILE_HDR.size)
            if len(raw) != FILE_HDR.size:
                raise ValueError("short file header")
            (magic, version, header_bytes, fs, ch_count, frame_samples, fmt,
             flags, start_utc, period_ns, u_scale, i_scale,
             reserved) = FILE_HDR.unpack(raw)
            if magic != FILE_MAGIC:
                raise ValueError("bad file magic %r" % (magic,))
            if fmt not in BYTES_PER_SAMPLE:
                raise ValueError("bad format id %d" % fmt)
            names = list(channel_names_from_manifest(path, ch_count))
            if channels is None:
                channels = names
            self.fs = fs
            self.fmt = fmt
            self.channel_count = ch_count
            self.frame_samples = frame_samples
            self.start_utc_ns = start_utc
            self.file_flags = flags
            self.u_scale = u_scale
            self.i_scale = i_scale
            self.names = names
            self.data = dict((n, array("d")) for n in channels if n in names)
            self.frame_count = 0
            end_off = self.size - END_REC.size
            bps = BYTES_PER_SAMPLE[fmt]
            off = FILE_HDR.size
            while off < end_off:
                fh.seek(off)
                fraw = fh.read(FRAME_HDR.size)
                if len(fraw) != FRAME_HDR.size:
                    raise ValueError("truncated frame header at %d" % off)
                (fmagic, fver, fhbytes, findex, start_sample, fstart, fend,
                 frames, fflags, fch, fres, payload_bytes) = \
                    FRAME_HDR.unpack(fraw)
                if fmagic != FRAME_MAGIC:
                    raise ValueError("bad frame magic at %d" % off)
                off += FRAME_HDR.size
                payload = fh.read(payload_bytes)
                if len(payload) != payload_bytes:
                    raise ValueError("truncated payload at %d" % off)
                off += payload_bytes
                plane = frames * bps
                for pos, name in enumerate(names):
                    if name not in self.data:
                        continue
                    chunk = payload[pos * plane:(pos + 1) * plane]
                    if fmt == 2:
                        scale = u_scale if pos < 3 else i_scale
                    else:
                        scale = 1.0
                    self.data[name].extend(
                        decode_plane(chunk, frames, fmt, scale))
                self.frame_count += 1
            self.end_off = off
            self.end_expected = end_off

    @property
    def sample_count(self):
        if not self.data:
            return 0
        return len(next(iter(self.data.values())))

    @property
    def duration_s(self):
        return self.sample_count / float(self.fs)

    def array(self, name):
        return self.data[name]


def manifest_document(path):
    candidates = [path + ".manifest.json",
                  os.path.splitext(path)[0] + ".manifest.json"]
    for cand in candidates:
        if not os.path.isfile(cand):
            continue
        try:
            with open(cand, "r", encoding="utf-8") as fh:
                return json.load(fh)
        except (OSError, ValueError):
            continue
    return {}


def channel_names_from_manifest(path, ch_count):
    candidates = [path + ".manifest.json",
                  os.path.splitext(path)[0] + ".manifest.json"]
    for cand in candidates:
        if not os.path.isfile(cand):
            continue
        try:
            with open(cand, "r", encoding="utf-8") as fh:
                doc = json.load(fh)
        except (OSError, ValueError):
            continue
        fp = doc.get("final_params")
        cfg = doc.get("config")
        names = None
        if isinstance(fp, dict):
            names = fp.get("channels")
        if not names and isinstance(cfg, dict):
            names = cfg.get("channels")
        if isinstance(names, list) and len(names) == ch_count:
            return [str(n) for n in names]
    return CHANNEL_NAMES[:ch_count]


# --------------------------------------------------------------------------
# measurements
# --------------------------------------------------------------------------

def block_rms(arr, i0, i1):
    i0 = max(0, int(i0))
    i1 = min(len(arr), int(i1))
    n = i1 - i0
    if n <= 0:
        return None
    total = 0.0
    for x in arr[i0:i1]:
        total += x * x
    return math.sqrt(total / n)


def rising_crossing_times(arr, fs, i0, i1):
    """Rising zero crossings with linear interpolation, in seconds."""
    i0 = max(0, int(i0))
    i1 = min(len(arr), int(i1))
    if i1 - i0 < 2:
        return []
    times = []
    prev = arr[i0]
    for i in range(i0 + 1, i1):
        cur = arr[i]
        if prev < 0.0 <= cur:
            denom = cur - prev
            frac = (-prev) / denom if denom != 0.0 else 0.0
            times.append((i - 1 + frac) / fs)
        prev = cur
    return times


def cycle_freqs(arr, fs, t0, t1):
    """Per-cycle frequency 1/(t[k+1]-t[k]) and its mid time, in seconds."""
    times = rising_crossing_times(arr, fs, round(t0 * fs), round(t1 * fs))
    out = []
    for i in range(len(times) - 1):
        dt = times[i + 1] - times[i]
        if dt > 0.0:
            out.append((0.5 * (times[i] + times[i + 1]), 1.0 / dt))
    return out


def crossing_freq(arr, fs, t0, t1):
    """Window frequency from a least-squares fit of crossing times."""
    times = rising_crossing_times(arr, fs, round(t0 * fs), round(t1 * fs))
    count = len(times)
    if count < 2:
        return None, count
    if count == 2:
        period = times[1] - times[0]
    else:
        m = count
        mean_k = (m - 1) / 2.0
        mean_t = sum(times) / m
        num = sum((i - mean_k) * (t - mean_t) for i, t in enumerate(times))
        den = sum((i - mean_k) ** 2 for i in range(m))
        period = num / den
    return (1.0 / period if period > 0.0 else None), count


def goertzel(arr, i0, n, fs, freq):
    """Complex X = sum x[k] exp(-j 2 pi f k / fs) (Goertzel recurrence)."""
    w = TWO_PI * freq / fs
    coeff = 2.0 * math.cos(w)
    s1 = 0.0
    s2 = 0.0
    for i in range(i0, i0 + n):
        s0 = arr[i] + coeff * s1 - s2
        s2 = s1
        s1 = s0
    re = s1 - s2 * math.cos(w)
    im = s2 * math.sin(w)
    # The recurrence leaves a linear phase e^{j (n-1) w}; remove it.
    phi = (n - 1) * w
    c = math.cos(phi)
    s = math.sin(phi)
    return (re * c + im * s, im * c - re * s)


def tone_rms(arr, fs, freq, t0, t1):
    """RMS of one frequency component over an integer-cycle window.

    Returns (rms, n_samples, start_sample).  Returns (None, 0, i0) when the
    window is too short or the frequency is outside (0, fs/2).
    """
    i0 = max(0, int(round(t0 * fs)))
    i1 = min(len(arr), int(round(t1 * fs)))
    avail = i1 - i0
    if avail < 2 or freq <= 0.0 or freq >= fs / 2.0:
        return None, 0, i0
    cycles = int(avail * freq / fs)
    if cycles >= 1:
        n = int(round(cycles * fs / freq))
        n = max(2, min(n, avail))
    else:
        n = avail
    re, im = goertzel(arr, i0, n, fs, freq)
    return math.sqrt(2.0) * math.hypot(re, im) / n, n, i0


def zero_runs(arr, fs, min_len_s=0.001, eps=1e-9):
    """Contiguous runs where |x| <= eps, longer than min_len_s."""
    min_len = max(1, int(min_len_s * fs))
    runs = []
    start = None
    for i, x in enumerate(arr):
        if abs(x) <= eps:
            if start is None:
                start = i
        elif start is not None:
            if i - start >= min_len:
                runs.append((start, i))
            start = None
    if start is not None and len(arr) - start >= min_len:
        runs.append((start, len(arr)))
    return [(s / fs, e / fs) for s, e in runs]


def crossing_grid_residual(arr, fs, t_ref0, t_ref1, t_eval):
    """Distance from t_eval to the nearest point of a fitted crossing grid.

    The rising-crossing grid t_k = a + k*T is fitted on the reference
    interval [t_ref0, t_ref1], which must lie in a steady part of the same
    channel.  Returns (|t_eval - nearest grid point|, T).
    """
    times = rising_crossing_times(arr, fs, round(t_ref0 * fs),
                                  round(t_ref1 * fs))
    m = len(times)
    if m < 3:
        return None, None
    mean_k = (m - 1) / 2.0
    mean_t = sum(times) / m
    num = sum((i - mean_k) * (t - mean_t) for i, t in enumerate(times))
    den = sum((i - mean_k) ** 2 for i in range(m))
    period = num / den
    if period <= 0.0:
        return None, None
    a = mean_t - period * mean_k
    k = round((t_eval - a) / period)
    return abs(t_eval - (a + k * period)), period


# --------------------------------------------------------------------------
# analytic reference for the A1.1.1 / A2.5.1 triangles
# --------------------------------------------------------------------------

def triangle(t, lo, hi, period):
    half = period * 0.5
    pos = math.fmod(t, period)
    if pos < 0.0:
        pos += period
    if pos <= half:
        ratio = pos / half
    else:
        ratio = (period - pos) / half
    return lo + (hi - lo) * ratio


def triangle_mean(a, b, lo, hi, period, steps=400):
    h = (b - a) / steps
    acc = 0.0
    for i in range(steps):
        acc += triangle(a + (i + 0.5) * h, lo, hi, period)
    return acc / steps


def linfit(xs, ys):
    n = len(xs)
    mx = sum(xs) / n
    my = sum(ys) / n
    sxx = sum((v - mx) ** 2 for v in xs)
    sxy = sum((xs[i] - mx) * (ys[i] - my) for i in range(n))
    slope = sxy / sxx
    return slope, my - slope * mx


# --------------------------------------------------------------------------
# mathematical acceptance / trajectory cases
# --------------------------------------------------------------------------

def case_a111(path):
    """A1.1.1: frequency triangle 42.5 -> 57.5 (5 s) -> 42.5 (5 s), repeating.

    A 1 s window mean equals the triangle value at the window centre, so the
    analytic window mean is used per window; the ramp endpoints are obtained
    by extrapolating a linear fit of the ascending / descending windows.
    """
    chk = Checks()
    wf = Waveform(path, ["Ua"])
    fs = wf.fs
    x = wf.array("Ua")
    win = 1.0
    n_win = int(math.floor(wf.duration_s / win))
    measured = [crossing_freq(x, fs, k * win, (k + 1) * win)[0]
                for k in range(n_win)]
    expected = [triangle_mean(k * win, (k + 1) * win, 42.5, 57.5, 10.0)
                for k in range(n_win)]

    for k in range(min(n_win, 10)):
        m = measured[k]
        chk.ok("freq.window_%d_%ds" % (k, k + 1),
               m is not None and abs(m - expected[k]) <= TRAJ_FREQ_HZ,
               rr(expected[k], 4), rr(m, 4), TRAJ_FREQ_HZ, "Hz",
               "frequency trajectory: window mean vs analytic triangle mean")

    rise = measured[0:5]
    fall = measured[5:10]
    chk.ok("freq.monotonic_rise_0_5s",
           all(v is not None for v in rise)
           and all(rise[i] < rise[i + 1] for i in range(len(rise) - 1)),
           "strictly increasing", [rr(v, 3) for v in rise])
    chk.ok("freq.monotonic_fall_5_10s",
           all(v is not None for v in fall)
           and all(fall[i] > fall[i + 1] for i in range(len(fall) - 1)),
           "strictly decreasing", [rr(v, 3) for v in fall])

    # Endpoint extrapolation (window centres 0.5 .. 4.5 and 5.5 .. 9.5).
    centres_up = [k + 0.5 for k in range(5)]
    slope_up, icpt_up = linfit(centres_up, rise)
    f_at_0 = icpt_up
    f_at_5_up = icpt_up + slope_up * 5.0
    chk.ok("freq.endpoint_t0", abs(f_at_0 - 42.5) <= TRAJ_FREQ_HZ, 42.5,
           rr(f_at_0, 4), TRAJ_FREQ_HZ, "Hz",
           "trajectory endpoint from 0-5 s windows")
    chk.ok("freq.endpoint_t5_rise", abs(f_at_5_up - 57.5) <= TRAJ_FREQ_HZ,
           57.5, rr(f_at_5_up, 4), TRAJ_FREQ_HZ, "Hz")

    centres_dn = [k + 0.5 for k in range(5, 10)]
    slope_dn, icpt_dn = linfit(centres_dn, fall)
    f_at_5_dn = icpt_dn + slope_dn * 5.0
    f_at_10 = icpt_dn + slope_dn * 10.0
    chk.ok("freq.endpoint_t5_fall", abs(f_at_5_dn - 57.5) <= TRAJ_FREQ_HZ,
           57.5, rr(f_at_5_dn, 4), TRAJ_FREQ_HZ, "Hz")
    chk.ok("freq.endpoint_t10", abs(f_at_10 - 42.5) <= TRAJ_FREQ_HZ, 42.5,
           rr(f_at_10, 4), TRAJ_FREQ_HZ, "Hz",
           "trajectory endpoint from 5-10 s windows")

    # Direct instantaneous extrema from single-cycle measurements.
    cycles = cycle_freqs(x, fs, 0.0, min(11.0, wf.duration_s))
    peak_t, peak = max(cycles, key=lambda c: c[1])
    trough_t, trough = min(cycles, key=lambda c: c[1])
    chk.ok("freq.peak_instantaneous",
           abs(peak - 57.5) <= TRAJ_FREQ_HZ, 57.5, rr(peak, 4), TRAJ_FREQ_HZ,
           "Hz", "highest single-cycle frequency at t=%.3f s" % peak_t)
    chk.ok("freq.trough_instantaneous",
           abs(trough - 42.5) <= TRAJ_FREQ_HZ, 42.5, rr(trough, 4),
           TRAJ_FREQ_HZ, "Hz",
           "lowest single-cycle frequency at t=%.3f s" % trough_t)

    return {
        "case": "A1.1.1",
        "file": wf.path,
        "values": {
            "sample_rate_hz": fs,
            "duration_s": rr(wf.duration_s, 3),
            "window_s": win,
            "window_freq_hz": [rr(v, 4) for v in measured],
            "expected_window_freq_hz": [rr(v, 4) for v in expected],
            "endpoint_t0_hz": rr(f_at_0, 4),
            "endpoint_t5_rise_hz": rr(f_at_5_up, 4),
            "endpoint_t5_fall_hz": rr(f_at_5_dn, 4),
            "endpoint_t10_hz": rr(f_at_10, 4),
            "peak_hz": rr(peak, 4),
            "peak_time_s": rr(peak_t, 6),
            "trough_hz": rr(trough, 4),
            "trough_time_s": rr(trough_t, 6),
            "rise_slope_hz_per_s": rr(slope_up, 4),
            "fall_slope_hz_per_s": rr(slope_dn, 4),
        },
        "checks": chk.items,
        "status": chk.status(),
    }


def case_a251(path):
    """A2.5.1: amplitude triangle 10 -> 80 -> 10 % over 120 s at 50.125 Hz."""
    chk = Checks()
    wf = Waveform(path, ["Ua"])
    fs = wf.fs
    x = wf.array("Ua")
    nominal = 220.0

    points = [(0.0, 10.0), (60.0, 80.0), (120.0, 10.0)]
    measured = {}
    expected_v = {}
    for t, pct in points:
        amp, n, i0 = tone_rms(x, fs, 50.125, t, t + 0.2)
        measured[t] = amp
        # Analytic triangle mean over the actual integer-cycle window.
        exp_pct = triangle_mean(t, t + n / float(fs), 10.0, 80.0, 120.0)
        exp_v = nominal * exp_pct / 100.0
        expected_v[t] = exp_v
        rel = abs(amp - exp_v) / exp_v if amp is not None and exp_v > 0 else 1.0
        chk.ok("rms.Ua_t%ds" % int(t),
               amp is not None and rel <= MATH_RMS_REL,
               rr(exp_v, 3), rr(amp, 3), MATH_RMS_REL, "V",
               "analytic amplitude trajectory over the %d-sample window, "
               "relative error %.3g" % (n, rel))

    freqs = []
    n_win = int(math.floor(wf.duration_s / 10.0))
    for k in range(n_win):
        f, _ = crossing_freq(x, fs, k * 10.0, (k + 1) * 10.0)
        freqs.append(f)
        chk.ok("freq.10s_window_%d" % k,
               f is not None and abs(f - 50.125) <= MATH_FREQ_HZ,
               50.125, rr(f, 5), MATH_FREQ_HZ, "Hz")

    return {
        "case": "A2.5.1",
        "file": wf.path,
        "values": {
            "sample_rate_hz": fs,
            "duration_s": rr(wf.duration_s, 3),
            "rms_Ua_hz_50.125": dict((str(t), rr(v, 3))
                                     for t, v in measured.items()),
            "expected_rms_V": dict((str(t), rr(v, 3))
                                   for t, v in expected_v.items()),
            "freq_10s_windows_hz": [rr(v, 5) for v in freqs],
        },
        "checks": chk.items,
        "status": chk.status(),
    }


def case_a421(path):
    """A4.2.1: sequential three-phase 0 % dip and reverse recovery.

    Structure criteria (no steady phase verdict):
      1. each phase shows one sustained 0 V interval (>= 20 ms, level < 0.5 V);
      2. onset order Ua -> Ub -> Uc, recovery order Uc -> Ub -> Ua;
      3. each switching instant lies on that phase's own rising zero crossing
         within one sample period (1/fs = 19.53 us at 51200 Hz, grid limit).
    The realised inter-phase spacing is recorded, not judged.
    """
    chk = Checks()
    wf = Waveform(path, ["Ua", "Ub", "Uc"])
    fs = wf.fs
    names = ["Ua", "Ub", "Uc"]
    tol = 1.0 / fs
    ref0, ref1 = 0.60, 1.00      # steady 100 % interval before the dip

    dips = {}
    residuals = {}
    periods = {}
    for name in names:
        x = wf.array(name)
        runs = zero_runs(x, fs)
        runs.sort(key=lambda r: r[1] - r[0])
        dips[name] = runs[-1] if runs else None
        chk.ok("dip.%s_single_interval" % name, len(runs) == 1,
               "1 sustained 0 V interval", len(runs))
        if not runs:
            continue
        s, e = runs[-1]
        mid = 0.5 * (s + e)
        dip_rms = block_rms(x, round((mid - 0.005) * fs),
                            round((mid + 0.005) * fs))
        chk.ok("dip.%s_duration" % name, (e - s) >= 0.020, ">= 0.020",
               rr(e - s, 6), None, "s")
        chk.ok("dip.%s_zero_level" % name,
               dip_rms is not None and dip_rms < 0.5, "< 0.5",
               rr(dip_rms, 6), 0.5, "V",
               "10 ms RMS at dip centre")

        r_start, period = crossing_grid_residual(x, fs, ref0, ref1, s)
        r_end, _ = crossing_grid_residual(x, fs, ref0, ref1, e)
        residuals[name] = (r_start, r_end)
        periods[name] = period
        chk.ok("dip.%s_start_on_own_zero_crossing" % name,
               r_start is not None and r_start <= tol + 1e-9,
               "<= 1 sample (%.4f us)" % (tol * 1e6),
               None if r_start is None else rr(r_start * 1e6, 4), tol * 1e6,
               "us")
        chk.ok("dip.%s_end_on_own_zero_crossing" % name,
               r_end is not None and r_end <= tol + 1e-9,
               "<= 1 sample (%.4f us)" % (tol * 1e6),
               None if r_end is None else rr(r_end * 1e6, 4), tol * 1e6,
               "us")

    intervals = {}
    if all(dips[n] for n in names):
        on = [dips[n][0] for n in names]
        off = [dips[n][1] for n in names]
        chk.ok("dip.onset_order", on[0] < on[1] < on[2],
               "Ua < Ub < Uc", [rr(v, 6) for v in on])
        chk.ok("dip.recovery_order", off[0] > off[1] > off[2],
               "Uc < Ub < Ua", [rr(v, 6) for v in off])
        intervals = {
            "onset_Ub_minus_Ua_ms": rr((on[1] - on[0]) * 1e3, 3),
            "onset_Uc_minus_Ub_ms": rr((on[2] - on[1]) * 1e3, 3),
            "recovery_Ua_minus_Ub_ms": rr((off[0] - off[1]) * 1e3, 3),
            "recovery_Ub_minus_Uc_ms": rr((off[1] - off[2]) * 1e3, 3),
        }

    return {
        "case": "A4.2.1",
        "file": wf.path,
        "values": {
            "sample_rate_hz": fs,
            "dip_start_s": dict((n, rr(dips[n][0], 6) if dips[n] else None)
                                for n in names),
            "dip_end_s": dict((n, rr(dips[n][1], 6) if dips[n] else None)
                              for n in names),
            "dip_duration_ms": dict(
                (n, rr((dips[n][1] - dips[n][0]) * 1e3, 3) if dips[n]
                 else None) for n in names),
            "switching_residual_us": dict(
                (n, {"start": None if residuals[n][0] is None
                     else rr(residuals[n][0] * 1e6, 4),
                     "end": None if residuals[n][1] is None
                     else rr(residuals[n][1] * 1e6, 4)})
                for n in names if n in residuals),
            "zero_crossing_period_s": dict(
                (n, rr(periods[n], 9)) for n in names if periods.get(n)),
            "alignment_tolerance_us": rr(tol * 1e6, 4),
            "interphase_intervals_ms": intervals,
            "interphase_note": "recorded only, not judged",
        },
        "checks": chk.items,
        "status": chk.status(),
    }


def case_a512(path):
    """A5.1.2: 73/80/87 % of 220 V, symmetric phase."""
    chk = Checks()
    wf = Waveform(path, ["Ua", "Ub", "Uc"])
    fs = wf.fs
    expected = {"Ua": 160.6, "Ub": 176.0, "Uc": 191.4}
    measured = {}
    for name in ("Ua", "Ub", "Uc"):
        x = wf.array(name)
        r = block_rms(x, 0, len(x))
        g = tone_rms(x, fs, 50.0, 0.0, wf.duration_s)[0]
        measured[name] = {"rms": r, "fundamental": g}
        chk.ok("rms.%s" % name,
               r is not None
               and abs(r - expected[name]) / expected[name] <= MATH_RMS_REL,
               expected[name], rr(r, 3), MATH_RMS_REL, "V",
               "analytic RMS, relative tolerance")
        chk.ok("rms.%s_goertzel" % name,
               g is not None
               and abs(g - expected[name]) / expected[name] <= MATH_RMS_REL,
               expected[name], rr(g, 3), MATH_RMS_REL, "V",
               "50 Hz component, integer-cycle window")

    return {
        "case": "A5.1.2",
        "file": wf.path,
        "values": {
            "sample_rate_hz": fs,
            "duration_s": rr(wf.duration_s, 3),
            "rms_V": dict((n, rr(v["rms"], 3))
                          for n, v in measured.items()),
            "expected_rms_V": expected,
        },
        "checks": chk.items,
        "status": chk.status(),
    }


def case_a622(path):
    """A6.2.2: fundamental 220 V plus a 3rd harmonic of 10 %."""
    chk = Checks()
    wf = Waveform(path, ["Ua"])
    fs = wf.fs
    x = wf.array("Ua")
    t1 = wf.duration_s
    fund, n1, _ = tone_rms(x, fs, 50.0, 0.0, t1)
    h3, n3, _ = tone_rms(x, fs, 150.0, 0.0, t1)
    ratio = 100.0 * h3 / fund if fund else None

    chk.ok("fundamental_rms",
           fund is not None and abs(fund - 220.0) / 220.0 <= MATH_RMS_REL,
           220.0, rr(fund, 3), MATH_RMS_REL, "V",
           "analytic fundamental RMS, relative tolerance")
    chk.ok("h3_rms_vs_fundamental",
           ratio is not None and abs(ratio - 10.0) / 10.0 <= MATH_HARM_REL,
           10.0, rr(ratio, 4), MATH_HARM_REL, "%",
           "H3 RMS relative to fundamental RMS")
    chk.ok("h3_rms",
           h3 is not None and abs(h3 - 22.0) / 22.0 <= MATH_HARM_REL, 22.0,
           rr(h3, 3), MATH_HARM_REL, "V", "10 % of 220 V")

    return {
        "case": "A6.2.2",
        "file": wf.path,
        "values": {
            "sample_rate_hz": fs,
            "duration_s": rr(wf.duration_s, 3),
            "fundamental_rms_V": rr(fund, 3),
            "h3_rms_V": rr(h3, 3),
            "h3_ratio_pct": rr(ratio, 4),
            "goertzel_window_samples": {"fundamental": n1, "h3": n3},
        },
        "checks": chk.items,
        "status": chk.status(),
    }


def case_a813(path):
    """A8.1.3: 1060 Hz carrier at 3 % (6.6 V RMS) on a 220 V fundamental."""
    chk = Checks()
    wf = Waveform(path, ["Ua"])
    fs = wf.fs
    x = wf.array("Ua")
    t1 = wf.duration_s
    fund, n1, _ = tone_rms(x, fs, 50.0, 0.0, t1)
    carrier, n2, _ = tone_rms(x, fs, 1060.0, 0.0, t1)
    ratio = 100.0 * carrier / fund if fund else None

    chk.ok("fundamental_rms",
           fund is not None and abs(fund - 220.0) / 220.0 <= MATH_RMS_REL,
           220.0, rr(fund, 3), MATH_RMS_REL, "V",
           "analytic fundamental RMS, relative tolerance")
    chk.ok("carrier_1060Hz_rms",
           carrier is not None and abs(carrier - 6.6) / 6.6 <= MATH_HARM_REL,
           6.6, rr(carrier, 3), MATH_HARM_REL, "V")
    chk.ok("carrier_ratio_pct",
           ratio is not None and abs(ratio - 3.0) / 3.0 <= MATH_HARM_REL, 3.0,
           rr(ratio, 4), MATH_HARM_REL, "%")

    return {
        "case": "A8.1.3",
        "file": wf.path,
        "values": {
            "sample_rate_hz": fs,
            "duration_s": rr(wf.duration_s, 3),
            "fundamental_rms_V": rr(fund, 3),
            "carrier_1060Hz_rms_V": rr(carrier, 3),
            "carrier_ratio_pct": rr(ratio, 4),
            "goertzel_window_samples": {"fundamental": n1, "carrier": n2},
        },
        "checks": chk.items,
        "status": chk.status(),
    }


def case_a651(path):
    """A6.5.1: H3 amplitude ramp 10 -> 0 -> 10 % (20 s period).

    Dynamic scenario: only the amplitude trajectory and file structure are
    checked.  No steady-state phase or amplitude verdict is produced.
    """
    chk = Checks()
    wf = Waveform(path, ["Ua"])
    fs = wf.fs
    x = wf.array("Ua")
    doc = manifest_document(path)
    cfg = doc.get("config") if isinstance(doc.get("config"), dict) else {}
    sig = doc.get("signal") if isinstance(doc.get("signal"), dict) else {}
    f0 = float(sig.get("freq_hz", 50.0))
    dur = wf.duration_s

    chk.ok("structure.file_layout", wf.end_off == wf.end_expected,
           "frames then end record", [wf.end_off, wf.end_expected])
    expected_samples = None
    if isinstance(cfg.get("duration_s"), (int, float)):
        expected_samples = int(round(float(cfg["duration_s"]) * fs))
    chk.ok("structure.sample_count",
           expected_samples is not None and wf.sample_count == expected_samples,
           expected_samples, wf.sample_count)
    chk.ok("structure.frame_count", wf.frame_count > 0, ">= 1", wf.frame_count)

    points = [0.1, 5.0, 10.0, 15.0, 20.0]
    last = min(max(dur - 0.4, 0.1), 30.0)
    if all(abs(last - t) > 0.05 for t in points):
        points.append(last)
    rows = []
    for t in sorted(points):
        if t + 0.2 > dur + 1e-9:
            continue
        fund, _, _ = tone_rms(x, fs, f0, t, t + 0.2)
        h3, n, _ = tone_rms(x, fs, 3.0 * f0, t, t + 0.2)
        ratio = 100.0 * h3 / fund if fund else None
        exp_pct = triangle_mean(t, t + n / float(fs), 10.0, 0.0, 20.0)
        tol = max(MATH_HARM_REL * abs(exp_pct), TRAJ_AMP_FLOOR_PCT)
        chk.ok("h3_ratio_t%.2fs" % t,
               ratio is not None and abs(ratio - exp_pct) <= tol,
               rr(exp_pct, 4), rr(ratio, 4), tol, "%",
               "analytic ramp trajectory")
        rows.append((t, ratio))

    down = [(t, r) for t, r in rows if t <= 10.05 and r is not None]
    up = [(t, r) for t, r in rows if 9.95 <= t <= 20.05 and r is not None]
    chk.ok("h3_ratio_decreasing_0_10s",
           len(down) >= 2
           and all(down[i][1] >= down[i + 1][1] - TRAJ_AMP_FLOOR_PCT
                   for i in range(len(down) - 1)),
           "10 -> 0 %", [rr(r, 4) for _, r in down])
    chk.ok("h3_ratio_increasing_10_20s",
           len(up) >= 2
           and all(up[i][1] <= up[i + 1][1] + TRAJ_AMP_FLOOR_PCT
                   for i in range(len(up) - 1)),
           "0 -> 10 %", [rr(r, 4) for _, r in up])

    return {
        "case": "A6.5.1",
        "file": wf.path,
        "values": {
            "sample_rate_hz": fs,
            "duration_s": rr(dur, 3),
            "fundamental_hz": rr(f0, 6),
            "h3_ratio_pct": dict((str(t), rr(r, 4)) for t, r in rows),
            "note": "dynamic ramp: trajectory and structure only",
        },
        "checks": chk.items,
        "status": chk.status(),
    }


CASES = {
    "a1-1-1": case_a111,
    "a2-5-1": case_a251,
    "a4-2-1": case_a421,
    "a5-1-2": case_a512,
    "a6-2-2": case_a622,
    "a6-5-1": case_a651,
    "a8-1-3": case_a813,
}


# --------------------------------------------------------------------------
# generic subcommands
# --------------------------------------------------------------------------

def cmd_rms(args):
    wf = Waveform(args.file, [args.channel])
    fs = wf.fs
    x = wf.array(args.channel)
    win = args.window
    n_win = int(math.floor(wf.duration_s / win))
    out = []
    for k in range(n_win):
        t0 = k * win
        r = block_rms(x, round(t0 * fs), round((t0 + win) * fs))
        out.append({"start_s": rr(t0, 6), "end_s": rr(t0 + win, 6),
                    "rms": rr(r, 6)})
    return {"command": "rms", "file": wf.path, "channel": args.channel,
            "window_s": win, "windows": out}


def cmd_freq(args):
    wf = Waveform(args.file, [args.channel])
    fs = wf.fs
    x = wf.array(args.channel)
    win = args.window
    t_end = wf.duration_s if args.end is None else min(args.end,
                                                       wf.duration_s)
    t0 = args.start
    out = []
    while t0 + win <= t_end + 1e-9:
        f, count = crossing_freq(x, fs, t0, min(t0 + win, t_end))
        out.append({"start_s": rr(t0, 6), "end_s": rr(t0 + win, 6),
                    "crossings": count, "freq_hz": rr(f, 6)})
        t0 += win
    return {"command": "freq", "file": wf.path, "channel": args.channel,
            "window_s": win, "windows": out}


def cmd_dft(args):
    wf = Waveform(args.file, [args.channel])
    fs = wf.fs
    x = wf.array(args.channel)
    t_end = wf.duration_s if args.end is None else min(args.end,
                                                       wf.duration_s)
    rms, n, i0 = tone_rms(x, fs, args.freq, args.start, t_end)
    return {"command": "dft", "file": wf.path, "channel": args.channel,
            "freq_hz": args.freq, "window_s": rr(t_end - args.start, 6),
            "integer_cycle_samples": n, "start_sample": i0,
            "rms": rr(rms, 6), "amplitude_peak": rr(
                rms * math.sqrt(2.0) if rms is not None else None, 6)}


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Independent mathematical measurements for waveform files.")
    parser.add_argument("command",
                        help="rms | freq | dft | " + " | ".join(sorted(CASES)))
    parser.add_argument("file", help="waveform .bin file")
    parser.add_argument("--channel", default="Ua")
    parser.add_argument("--window", type=float, default=None)
    parser.add_argument("--freq", type=float, default=None)
    parser.add_argument("--start", type=float, default=0.0)
    parser.add_argument("--end", type=float, default=None)
    parser.add_argument("--json", help="also write the JSON to this path")
    args = parser.parse_args(argv)

    if args.command in CASES:
        result = CASES[args.command](args.file)
    elif args.command == "rms":
        args.window = 0.2 if args.window is None else args.window
        result = cmd_rms(args)
    elif args.command == "freq":
        args.window = 1.0 if args.window is None else args.window
        result = cmd_freq(args)
    elif args.command == "dft":
        if args.freq is None:
            sys.stderr.write("dft requires --freq\n")
            return 2
        result = cmd_dft(args)
    else:
        sys.stderr.write("unknown command: %s\n" % args.command)
        return 2

    text = json.dumps(result, ensure_ascii=True, indent=2, allow_nan=False)
    sys.stdout.write(text + "\n")
    if args.json:
        with open(args.json, "w", encoding="utf-8") as fh:
            fh.write(text + "\n")
    return 0 if result.get("status", "PASS") == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
