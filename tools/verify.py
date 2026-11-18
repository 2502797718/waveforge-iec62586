#!/usr/bin/env python3
"""Independent mathematical verifier for generated waveform files (v1).

The on-disk layout is taken from docs/09-output-format.md.  This tool does
not read or import any generator source code and uses only the Python
standard library (math / struct / json / argparse).

Scope is mathematical fidelity only: manifest-driven reconstruct, file
structure, timestamps, DFT / RMS / phase.  No IEC limit, measurement
uncertainty or conformance verdict is produced.

Usage:
    python3 tools/verify.py <waveform.bin> [--manifest <path>]
                            [--expect <path>] [--report <json>]

Both manifest models are accepted:
  * ``program`` (components / modulators / events) takes priority;
  * the legacy ``signal`` block (freq / amp_pct / harmonics / ...) is still
    supported and mapped to the same internal component list.

Components carrying any modulator are dynamic: their amplitude/phase are
measured and reported with status SKIP ("DYNAMIC"), never judged.  Only
steady components are compared against the analytic expression rebuilt from
the manifest, with floating-point tolerances.

Exit status: 0 = PASS, 1 = FAIL, 2 = PARTIAL, 3 = usage / IO error.
"""

import argparse
import json
import math
import operator
import os
import struct
import sys
from array import array

FILE_MAGIC = b"WGIEC625"
FRAME_MAGIC = b"WGF1"
END_MAGIC = b"WGE1"

# docs/09 sections 2, 3, 4 (little endian, no padding)
FILE_HDR = struct.Struct("<8sHHIHHHHQdddQ")      # 64 bytes
FRAME_HDR = struct.Struct("<4sHHQQQQIIHHI")      # 56 bytes
END_REC = struct.Struct("<4sHHQQQ")              # 32 bytes

FILE_HDR_BYTES = FILE_HDR.size
FRAME_HDR_BYTES = FRAME_HDR.size
END_REC_BYTES = END_REC.size

CHANNEL_NAMES = ["Ua", "Ub", "Uc", "Ia", "Ib", "Ic", "In"]
DEFAULT_NOMINAL = [220.0, 220.0, 220.0, 5.0, 5.0, 5.0, 5.0]
FORMAT_NAMES = {0: "f64", 1: "f32", 2: "i24"}
BYTES_PER_SAMPLE = {0: 8, 1: 4, 2: 3}

# Mathematical reconstruction tolerances (floating point / quantisation,
# not IEC measurement limits).
TOL_RMS_REL = 0.001       # fundamental RMS relative error <= 0.1 %
TOL_FREQ_HZ = 0.001       # fundamental frequency error <= 1 mHz
TOL_PHASE_DEG = 0.1       # phase error <= 0.1 deg
TOL_HARM_REL = 0.005      # harmonic / interharmonic / tone RMS <= 0.5 %
TOL_TIME_NS = 0           # exact integer timestamps

MEASUREMENT_SCOPE = "math"

BLOCK_SECONDS = 5.0
BLOCK_MAX_SAMPLES = 1000000
EMPIRICAL_RMS_REL = 0.005
EMPIRICAL_FREQ_REL = 0.002


# --------------------------------------------------------------------------
# small helpers
# --------------------------------------------------------------------------

def ns_at(n, fs):
    """docs/09 section 6: integer half-up conversion, no floating point."""
    q, r = divmod(int(n), int(fs))
    return q * 1000000000 + (r * 1000000000 + fs // 2) // fs


def half_up(x):
    return int(math.floor(x + 0.5 + 1e-9))


def finite(x):
    if x is None:
        return None
    x = float(x)
    return x if math.isfinite(x) else None


def rr(x, nd=9):
    x = finite(x)
    return None if x is None else round(x, nd)


def norm_deg(a):
    a = math.fmod(float(a), 360.0)
    if a <= -180.0:
        a += 360.0
    elif a > 180.0:
        a -= 360.0
    return a


def wrap_diff(a, b):
    """Signed smallest difference a - b in degrees."""
    return norm_deg(float(a) - float(b))


def num(v):
    """Best-effort conversion of a manifest scalar to float."""
    if isinstance(v, bool):
        return None
    if isinstance(v, (int, float)):
        return float(v)
    if isinstance(v, str):
        s = v.strip()
        if s.endswith("%"):
            s = s[:-1]
        try:
            return float(s)
        except ValueError:
            return None
    return None


def lookup(v, name, idx):
    """Pick a per-channel value from a scalar / list / dict manifest field."""
    if isinstance(v, list):
        if 0 <= idx < len(v):
            return num(v[idx])
        return None
    if isinstance(v, dict):
        if name in v:
            return num(v[name])
        for key in (idx, str(idx)):
            if key in v:
                return num(v[key])
        return None
    return num(v)


def as_int(v):
    """Exact integer conversion (UTC ns values exceed 2**53)."""
    if isinstance(v, bool):
        return None
    if isinstance(v, int):
        return v
    if isinstance(v, float):
        if math.isfinite(v) and v == int(v):
            return int(v)
        return None
    if isinstance(v, str):
        try:
            return int(v.strip(), 10)
        except ValueError:
            return None
    return None


class Checks(object):
    def __init__(self):
        self.items = []

    def add(self, name, expected, actual, tolerance=None, status="PASS",
            unit=None, note=None):
        item = {
            "name": name,
            "expected": expected,
            "actual": actual,
            "tolerance": tolerance,
            "status": status,
        }
        if unit:
            item["unit"] = unit
        if note:
            item["note"] = note
        self.items.append(item)

    def ok(self, name, cond, expected=None, actual=None, tolerance=None,
           unit=None, note=None):
        self.add(name, expected, actual, tolerance,
                 "PASS" if cond else "FAIL", unit, note)
        return cond

    def status(self):
        states = [c["status"] for c in self.items]
        if "FAIL" in states:
            return "FAIL"
        if "SKIP" in states:
            return "PARTIAL"
        return "PASS"


# --------------------------------------------------------------------------
# manifest / expectations
# --------------------------------------------------------------------------

SIGNAL_KEYS = ("signal", "signal_config", "waveform")


def as_dict(value):
    """Accept a dict or a JSON-encoded string; anything else becomes {}."""
    if isinstance(value, str):
        try:
            value = json.loads(value)
        except ValueError:
            return {}
    return value if isinstance(value, dict) else {}


def find_manifest(bin_path):
    candidates = [
        bin_path + ".manifest.json",
        os.path.splitext(bin_path)[0] + ".manifest.json",
        os.path.join(os.path.dirname(os.path.abspath(bin_path)),
                     "manifest.json"),
    ]
    for path in candidates:
        if os.path.isfile(path):
            return path
    return None


def find_expectations(bin_path):
    candidates = [
        bin_path + ".expect.json",
        os.path.splitext(bin_path)[0] + ".expect.json",
        bin_path + ".signal.json",
    ]
    for path in candidates:
        if os.path.isfile(path):
            return path
    return None


def signal_block(*docs):
    for doc in docs:
        for key in SIGNAL_KEYS:
            block = as_dict(doc.get(key))
            if block:
                return block
    return {}


def parse_signal(sig, names):
    """Normalise a legacy signal block.

    Returns a dict with the scalar view (frequency_hz / amp_pct / phase_deg)
    plus component-like lists (harmonics / interharmonics / tones) and the
    dynamic hints derived from frequency profiles, amplitude profiles and
    legacy modulation.
    """
    nonsteady = False
    freq_dynamic = False
    amp_dynamic = False
    freq = None

    for key in ("freq_hz", "frequency_hz"):
        val = num(sig.get(key))
        if val is not None:
            freq = val
    profile = sig.get("freq_profile")
    freq_to = num(sig.get("freq_to_hz"))
    fr = sig.get("frequency")
    if isinstance(fr, dict):
        cur = num(fr.get("current"))
        start = num(fr.get("from"))
        end = num(fr.get("to", fr.get("from")))
        prof = str(fr.get("profile", "")).strip().lower()
        if cur is not None:
            freq = cur
        elif start is not None:
            freq = start
        if prof in ("linear", "triangle"):
            freq_dynamic = True
        if start is not None and end is not None and abs(end - start) > 1e-12:
            freq_dynamic = True
    elif isinstance(fr, (int, float)):
        freq = num(fr)
    if isinstance(profile, str):
        prof = profile.strip().lower()
        if prof in ("linear", "triangle"):
            freq_dynamic = True
    if freq is not None and freq_to is not None and abs(freq_to - freq) > 1e-12:
        freq_dynamic = True

    amp_raw = sig.get("amp_pct")
    amps = [lookup(amp_raw, name, i) for i, name in enumerate(names)]
    if isinstance(amp_raw, dict):
        for val in amp_raw.values():
            if isinstance(val, dict):
                start = num(val.get("from"))
                end = num(val.get("to", val.get("from")))
                if start is not None and end is not None and abs(end - start) > 1e-12:
                    amp_dynamic = True

    phase_raw = sig.get("phase_deg")
    phases = [lookup(phase_raw, name, i) for i, name in enumerate(names)]

    harmonics = []
    hraw = sig.get("harmonics")
    if isinstance(hraw, list):
        for item in hraw:
            if isinstance(item, dict):
                order = num(item.get("order"))
                amp = num(item.get("amp_pct", item.get("amplitude_pct")))
                if order is not None and amp is not None:
                    harmonics.append({
                        "order": order,
                        "amp_pct": amp,
                        "phase_deg": num(item.get("phase_deg")) or 0.0,
                    })

    interharmonics = []
    ihraw = sig.get("interharmonics")
    if isinstance(ihraw, list):
        for item in ihraw:
            if isinstance(item, dict):
                order = num(item.get("order"))
                amp = num(item.get("amp_pct", item.get("amplitude_pct")))
                if order is not None and amp is not None:
                    interharmonics.append({
                        "order": order,
                        "amp_pct": amp,
                        "phase_deg": num(item.get("phase_deg")) or 0.0,
                    })

    tones = []
    traw = sig.get("tones")
    if isinstance(traw, list):
        for item in traw:
            if isinstance(item, dict):
                f_hz = num(item.get("frequency_hz"))
                amp = num(item.get("amp_pct", item.get("amplitude_pct")))
                if f_hz is not None and amp is not None:
                    tones.append({
                        "frequency_hz": f_hz,
                        "amp_pct": amp,
                        "phase_deg": num(item.get("phase_deg")) or 0.0,
                    })

    mod = sig.get("modulation")
    if mod:
        if isinstance(mod, dict):
            kind = mod.get("type")
            if kind not in (None, "none", "NONE", 0):
                nonsteady = True
                amp_dynamic = True
        elif isinstance(mod, str):
            if mod.strip().lower() not in ("", "none"):
                nonsteady = True
                amp_dynamic = True

    if freq_dynamic or amp_dynamic:
        nonsteady = True

    return {
        "frequency_hz": freq,
        "amp_pct": amps,
        "phase_deg": phases,
        "harmonics": harmonics,
        "interharmonics": interharmonics,
        "tones": tones,
        "nonsteady": nonsteady,
        "frequency_dynamic": freq_dynamic,
        "amp_dynamic": amp_dynamic,
    }


def _kind_name(value):
    return str(value or "").strip().lower() or "fundamental"


def normalize_program(prog, names, nominal, base_freq, freq_dynamic):
    """Turn a manifest ``program`` block into a per-channel component list.

    ``dynamic`` marks a component whose amplitude or phase is not constant
    over time.  It is set for any modulator, for the global frequency
    profile, for phase/amplitude events, and is propagated to harmonics whose
    amplitude is referenced to a modulated fundamental.
    """
    components = []
    events = []
    raw_components = prog.get("components")
    if not isinstance(raw_components, list):
        raw_components = []

    for idx, raw in enumerate(raw_components):
        if not isinstance(raw, dict):
            continue
        kind = _kind_name(raw.get("kind"))
        order = num(raw.get("order"))
        frequency_hz = num(raw.get("frequency_hz"))
        amp_raw = raw.get("amp_pct")
        phase_raw = raw.get("phase_deg")
        channel_names = raw.get("channels")
        if not isinstance(channel_names, list) or not channel_names:
            channel_names = list(names)

        amp_pct = {}
        phase_deg = {}
        for i, name in enumerate(names):
            amp_pct[name] = lookup(amp_raw, name, i) or 0.0
            phase_deg[name] = lookup(phase_raw, name, i) or 0.0

        mods = []
        raw_mods = raw.get("modulators")
        if isinstance(raw_mods, list):
            for mod in raw_mods:
                if isinstance(mod, dict):
                    mods.append(_kind_name(mod.get("kind")))
        ref = str(raw.get("amplitude_ref", "nominal")).strip().lower()
        components.append({
            "index": idx,
            "kind": kind,
            "order": order,
            "frequency_hz": frequency_hz,
            "amp_pct": amp_pct,
            "phase_deg": phase_deg,
            "channels": [str(c) for c in channel_names],
            "amplitude_ref": "fundamental" if ref.startswith("fund") else "nominal",
            "modulators": mods,
            "amp_dynamic": any(m.startswith("amp_") for m in mods),
            "dynamic_own": bool(mods),
            "dynamic": bool(mods),
        })

    raw_events = prog.get("events")
    if isinstance(raw_events, list):
        for raw in raw_events:
            if isinstance(raw, dict):
                events.append({
                    "kind": _kind_name(raw.get("kind")),
                    "time_s": num(raw.get("time_s")),
                    "channel_mask": raw.get("channel_mask"),
                    "value": num(raw.get("value")),
                })

    has_freq_mod = any(m.startswith("freq_") for c in components
                       for m in c["modulators"])
    freq_dynamic = bool(freq_dynamic) or has_freq_mod or bool(events)
    fundamental = None
    for comp in components:
        if comp["kind"] == "fundamental":
            fundamental = comp
            comp["dynamic"] = (comp["dynamic_own"] or freq_dynamic)
            break
    fund_dynamic = fundamental["dynamic"] if fundamental is not None else freq_dynamic
    for comp in components:
        if comp["kind"] == "fundamental":
            continue
        if comp["kind"] == "harmonic":
            comp["dynamic"] = (comp["dynamic_own"] or freq_dynamic
                               or (comp["amplitude_ref"] == "fundamental"
                                   and fund_dynamic))
        elif comp["kind"] == "interharmonic":
            comp["dynamic"] = comp["dynamic_own"] or freq_dynamic
        else:  # tone: independent absolute frequency
            comp["dynamic"] = comp["dynamic_own"]

    return components, events, freq_dynamic


def legacy_components(sig_view, names, nominal):
    """Map the legacy ``signal`` block onto the normalized component list."""
    components = []
    amps = sig_view["amp_pct"]
    phases = sig_view["phase_deg"]
    freq_dynamic = bool(sig_view["frequency_dynamic"])
    base_dynamic = bool(sig_view["nonsteady"])

    components.append({
        "index": 0,
        "kind": "fundamental",
        "order": 1.0,
        "frequency_hz": sig_view["frequency_hz"],
        "amp_pct": dict((n, amps[i] if i < len(amps) and amps[i] is not None
                         else 0.0) for i, n in enumerate(names)),
        "phase_deg": dict((n, phases[i] if i < len(phases) and phases[i] is not None
                           else 0.0) for i, n in enumerate(names)),
        "channels": list(names),
        "amplitude_ref": "nominal",
        "modulators": [],
        "amp_dynamic": bool(sig_view["amp_dynamic"]),
        "dynamic_own": base_dynamic,
        "dynamic": base_dynamic,
    })
    for item in sig_view["harmonics"]:
        components.append({
            "index": len(components),
            "kind": "harmonic",
            "order": item["order"],
            "frequency_hz": None,
            "amp_pct": dict((n, item["amp_pct"]) for n in names),
            # Legacy harmonics add the fundamental channel phase.
            "phase_deg": dict(
                (n, item["phase_deg"]
                 + (phases[i] if i < len(phases) and phases[i] is not None
                    else 0.0))
                for i, n in enumerate(names)),
            "channels": list(names),
            "amplitude_ref": "fundamental",
            "modulators": [],
            "dynamic_own": False,
            "dynamic": base_dynamic,
        })
    for item in sig_view["interharmonics"]:
        components.append({
            "index": len(components),
            "kind": "interharmonic",
            "order": item["order"],
            "frequency_hz": None,
            "amp_pct": dict((n, item["amp_pct"]) for n in names),
            "phase_deg": dict((n, item["phase_deg"]) for n in names),
            "channels": list(names),
            "amplitude_ref": "nominal",
            "modulators": [],
            "dynamic_own": False,
            "dynamic": freq_dynamic,
        })
    for item in sig_view["tones"]:
        components.append({
            "index": len(components),
            "kind": "tone",
            "order": None,
            "frequency_hz": item["frequency_hz"],
            "amp_pct": dict((n, item["amp_pct"]) for n in names),
            "phase_deg": dict((n, item["phase_deg"]) for n in names),
            "channels": list(names),
            "amplitude_ref": "nominal",
            "modulators": [],
            "dynamic_own": False,
            "dynamic": False,
        })
    return components


def component_frequency(comp, base_freq):
    """Analytic frequency of a component for the steady case."""
    kind = comp["kind"]
    if kind == "fundamental":
        return base_freq
    if kind == "tone":
        return comp["frequency_hz"]
    if comp["order"] is None or base_freq is None:
        return None
    return comp["order"] * base_freq


def component_tag(comp):
    kind = comp["kind"]
    if kind == "fundamental":
        return "fundamental"
    if kind == "tone":
        return "tone%g" % comp["frequency_hz"] if comp["frequency_hz"] is not None \
            else "tone"
    if comp["order"] is None:
        return kind
    return "%s%g" % (kind, comp["order"])


def component_expected_rms(comp, name, nominal, fund_rms):
    amp = comp["amp_pct"].get(name, 0.0)
    if comp["kind"] == "fundamental":
        return nominal * amp / 100.0
    if comp["kind"] == "harmonic" and comp["amplitude_ref"] == "fundamental":
        return fund_rms * amp / 100.0
    return nominal * amp / 100.0


def parse_document(mf, channel_count):
    """Parse a manifest or an input configuration document (docs/08).

    The generator's manifest records the effective configuration in a flat
    ``config`` object (channels / nominal / duration_s / format / ...), while
    a docs/08 input document nests the stimulus under ``signal``.  Both are
    accepted; signal fields are simply absent when the document does not
    carry them.
    """
    if not isinstance(mf, dict):
        mf = {}
    cfg = as_dict(mf.get("config"))
    if not cfg:
        cfg = mf
    fp = as_dict(mf.get("final_params"))

    names = cfg.get("channels") or fp.get("channels") or mf.get("channels")
    if isinstance(names, list) and len(names) == channel_count:
        names = [str(n) for n in names]
    else:
        names = CHANNEL_NAMES[:channel_count]

    nominal_raw = (cfg.get("nominal") or fp.get("nominal")
                   or mf.get("nominal"))
    nominals = []
    nominal_defaulted = False
    for i, name in enumerate(names):
        val = lookup(nominal_raw, name, i)
        if val is None:
            base = CHANNEL_NAMES.index(name) if name in CHANNEL_NAMES else i
            val = DEFAULT_NOMINAL[base if base < len(DEFAULT_NOMINAL) else 0]
            nominal_defaulted = True
        nominals.append(val)

    sig = signal_block(cfg, mf)
    sig_view = parse_signal(sig, names)

    # merge segments in order: last segment wins for the reported values
    segments = cfg.get("segments")
    if not isinstance(segments, list):
        segments = mf.get("segments")
    if not isinstance(segments, list):
        segments = []
    if segments:
        sig_view["nonsteady"] = True
    for seg in segments:
        if isinstance(seg, dict):
            seg_sig = as_dict(seg.get("signal"))
            if seg_sig:
                merged = dict(sig)
                merged.update(seg_sig)
                seg_view = parse_signal(merged, names)
                if seg_view["frequency_hz"] is not None:
                    sig_view["frequency_hz"] = seg_view["frequency_hz"]
                sig_view["amp_pct"] = seg_view["amp_pct"]
                sig_view["phase_deg"] = seg_view["phase_deg"]
                if seg_view["harmonics"]:
                    sig_view["harmonics"] = seg_view["harmonics"]
                sig_view["nonsteady"] = (sig_view["nonsteady"]
                                         or seg_view["nonsteady"])

    freq = sig_view["frequency_hz"]
    amps = sig_view["amp_pct"]
    phases = sig_view["phase_deg"]

    prog = as_dict(mf.get("program")) or as_dict(cfg.get("program"))
    if prog:
        components, events, frequency_dynamic = normalize_program(
            prog, names, nominals, freq, sig_view["frequency_dynamic"])
        program_active = bool(components)
    else:
        components = legacy_components(sig_view, names, nominals)
        events = []
        frequency_dynamic = sig_view["frequency_dynamic"]
        program_active = False
    nonsteady = (bool(sig_view["nonsteady"]) or frequency_dynamic
                 or bool(events) or bool(segments))

    time_block = as_dict(cfg.get("time")) or as_dict(mf.get("time"))
    output_block = as_dict(cfg.get("output")) or as_dict(mf.get("output"))

    duration = None
    for candidate in (fp.get("duration_s"), cfg.get("duration_s"),
                      time_block.get("duration_s"), mf.get("duration_s")):
        val = num(candidate)
        if val is not None:
            duration = val
            break

    frame_samples = None
    for candidate in (fp.get("frame_samples"), cfg.get("frame_samples"),
                      output_block.get("frame_samples")):
        if frame_samples is None:
            frame_samples = as_int(candidate)

    sample_rate = None
    for candidate in (mf.get("sample_rate_hz"), cfg.get("sample_rate_hz"),
                      time_block.get("sample_rate_hz"),
                      fp.get("sample_rate_hz")):
        if sample_rate is None:
            sample_rate = as_int(candidate)

    start_utc = None
    for candidate in (mf.get("start_utc_ns"), cfg.get("start_utc_ns"),
                      time_block.get("start_utc_ns"),
                      fp.get("start_utc_ns")):
        if start_utc is None:
            start_utc = as_int(candidate)

    sample_count = None
    for candidate in (mf.get("sample_count"), cfg.get("sample_count"),
                      fp.get("sample_count")):
        if sample_count is None:
            sample_count = as_int(candidate)

    fmt = fp.get("format")
    if fmt is None:
        fmt = cfg.get("format")
    if fmt is None:
        fmt = output_block.get("format")

    return {
        "path": None,
        "channels": names,
        "nominal": nominals,
        "nominal_defaulted": nominal_defaulted,
        "amp_pct": amps,
        "amp_known": all(v is not None for v in amps),
        "phase_deg": phases,
        "frequency_hz": freq,
        "harmonics": sig_view["harmonics"],
        "components": components,
        "events": events,
        "program_active": program_active,
        "frequency_dynamic": frequency_dynamic,
        "duration_s": duration,
        "nonsteady_hint": nonsteady,
        "sample_rate_hz": sample_rate,
        "start_utc_ns": start_utc,
        "sample_count": sample_count,
        "format": fmt,
        "frame_samples": frame_samples,
        "output_file": mf.get("output_file"),
    }


def read_json(path):
    with open(path, "r", encoding="utf-8") as fh:
        return json.load(fh)


def read_manifest(path, channel_count):
    info = parse_document(read_json(path), channel_count)
    info["path"] = os.path.abspath(path)
    return info


def read_expectations(path, channel_count):
    info = parse_document(read_json(path), channel_count)
    info["path"] = os.path.abspath(path)
    return info


def merge_info(base, extra):
    """Fill the gaps of ``base`` (manifest) with ``extra`` (expectations)."""
    if base is None:
        return extra
    if extra is None:
        return base
    out = dict(base)
    for key in ("frequency_hz", "duration_s", "format", "frame_samples",
                "sample_rate_hz", "start_utc_ns", "sample_count",
                "output_file"):
        if out.get(key) is None and extra.get(key) is not None:
            out[key] = extra[key]
    if out.get("nominal_defaulted") and not extra.get("nominal_defaulted"):
        out["nominal"] = extra["nominal"]
        out["nominal_defaulted"] = False
    if not out.get("amp_known") and extra.get("amp_known"):
        out["amp_pct"] = extra["amp_pct"]
        out["amp_known"] = True
    if (all(v is None for v in out.get("phase_deg", []))
            and any(v is not None for v in extra.get("phase_deg", []))):
        out["phase_deg"] = extra["phase_deg"]
    if not out.get("harmonics") and extra.get("harmonics"):
        out["harmonics"] = extra["harmonics"]
    if not out.get("components") and extra.get("components"):
        out["components"] = extra["components"]
        out["program_active"] = extra.get("program_active", False)
    if not out.get("events") and extra.get("events"):
        out["events"] = extra["events"]
    if not out.get("frequency_dynamic") and extra.get("frequency_dynamic"):
        out["frequency_dynamic"] = True
    if not out.get("nonsteady_hint") and extra.get("nonsteady_hint"):
        out["nonsteady_hint"] = True
    return out


# --------------------------------------------------------------------------
# sample decoding and signal analysis
# --------------------------------------------------------------------------

def decode_samples(raw, frames, fmt, scale):
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
        return vals
    # signed 24-bit little endian
    out = [0.0] * frames
    for i in range(frames):
        j = 3 * i
        v = raw[j] | (raw[j + 1] << 8) | (raw[j + 2] << 16)
        if v & 0x800000:
            v -= 0x1000000
        out[i] = v * scale
    return out


class ChannelAcc(object):
    __slots__ = ("n", "total", "total_sq", "block", "cap",
                 "first_frame", "last_frame")

    def __init__(self, cap):
        self.n = 0
        self.total = 0.0
        self.total_sq = 0.0
        self.block = array("d")
        self.cap = cap
        self.first_frame = None
        self.last_frame = None

    def add(self, vals):
        n = len(vals)
        if n == 0:
            return
        total = math.fsum(vals)
        total_sq = math.fsum(map(operator.mul, vals, vals))
        self.n += n
        self.total += total
        self.total_sq += total_sq
        frame = (n, total, total_sq)
        if self.first_frame is None:
            self.first_frame = frame
        self.last_frame = frame
        if len(self.block) < self.cap:
            need = self.cap - len(self.block)
            chunk = vals[:need]
            if isinstance(chunk, array) and chunk.typecode != "d":
                chunk = array("d", chunk)
            self.block.extend(chunk)

    def rms(self):
        if not self.n:
            return None
        return math.sqrt(self.total_sq / self.n)

    @staticmethod
    def _frame_rms(frame):
        if not frame or not frame[0]:
            return None
        return math.sqrt(frame[2] / frame[0])


def crossing_analysis(block, fs):
    """Rising zero crossings with linear interpolation."""
    times = []
    if len(block) < 3:
        return {"count": 0, "freq": None, "spread": None}
    prev = block[0]
    for i in range(1, len(block)):
        cur = block[i]
        if prev < 0.0 <= cur:
            denom = cur - prev
            frac = (-prev) / denom if denom != 0.0 else 0.0
            times.append((i - 1 + frac) / fs)
        prev = cur
    count = len(times)
    if count < 2:
        return {"count": count, "freq": None, "spread": None}
    if count == 2:
        period = times[1] - times[0]
    else:
        m = count
        mean_k = (m - 1) / 2.0
        mean_t = math.fsum(times) / m
        num = math.fsum((i - mean_k) * (t - mean_t)
                        for i, t in enumerate(times))
        den = math.fsum((i - mean_k) ** 2 for i in range(m))
        period = num / den
    freq = 1.0 / period if period > 0.0 else None
    intervals = [times[i + 1] - times[i] for i in range(count - 1)]
    mean_dt = math.fsum(intervals) / len(intervals)
    spread = None
    if mean_dt > 0.0:
        spread = (max(intervals) - min(intervals)) / mean_dt
    return {"count": count, "freq": freq, "spread": spread}


def goertzel(block, fs, freq):
    """Complex amplitude sum X = sum x[n] * exp(-j*2*pi*f*n/fs)."""
    n = len(block)
    if n < 2 or freq <= 0.0 or freq >= fs / 2.0:
        return None
    w = 2.0 * math.pi * freq / fs
    coeff = 2.0 * math.cos(w)
    s1 = 0.0
    s2 = 0.0
    for x in block:
        s0 = x + coeff * s1 - s2
        s2 = s1
        s1 = s0
    real = s1 - s2 * math.cos(w)
    imag = s2 * math.sin(w)
    # The recurrence yields G = sum x[n] e^{-j*w*n} * e^{j*w*(n-1)}
    # (see Chebyshev form); remove that linear phase.
    phi = (n - 1) * w
    c = math.cos(phi)
    s = math.sin(phi)
    return (real * c + imag * s, imag * c - real * s)


def spectral_measure(block, fs, freq, orders):
    """Return (fund_rms, phase_deg, {order: rms}) on an integer-cycle block."""
    if not block or freq is None or freq <= 0.0 or freq >= fs / 2.0:
        return None, None, {}
    total = len(block)
    cycles = int(total * freq / fs)
    if cycles >= 1:
        n = int(round(cycles * fs / freq))
        n = max(2, min(n, total))
    else:
        n = total
    window = block[:n]
    comp = goertzel(window, fs, freq)
    if comp is None:
        return None, None, {}
    real, imag = comp
    fund_rms = math.sqrt(2.0) * math.hypot(real, imag) / n
    # goertzel() returns X = sum x[n] * exp(-j*w*n); for x = A*cos(w*n + phi)
    # an integer-cycle window yields X = A/2 * n * exp(j*phi), so the phase
    # is atan2(Im, Re) directly (cosine reference, 0 deg at t = 0).
    phase = norm_deg(math.degrees(math.atan2(imag, real)))
    measured = {}
    for order in orders:
        f = order * freq
        if f >= fs / 2.0:
            measured[order] = None
            continue
        comp = goertzel(window, fs, f)
        if comp is None:
            measured[order] = None
        else:
            measured[order] = (math.sqrt(2.0) *
                               math.hypot(comp[0], comp[1]) / n)
    return fund_rms, phase, measured


def tone_measure(block, fs, freq):
    """RMS and phase of one steady component over an integer-cycle window.

    The phasor is referenced to sample 0 (cosine reference, phase 0 deg at
    t = 0), matching the analytic manifest expression.
    """
    if not block or freq is None or freq <= 0.0 or freq >= fs / 2.0:
        return None, None
    total = len(block)
    cycles = int(total * freq / fs)
    if cycles >= 1:
        n = int(round(cycles * fs / freq))
        n = max(2, min(n, total))
    else:
        n = total
    comp = goertzel(block[:n], fs, freq)
    if comp is None:
        return None, None
    real, imag = comp
    rms = math.sqrt(2.0) * math.hypot(real, imag) / n
    phase = norm_deg(math.degrees(math.atan2(imag, real)))
    return rms, phase


def analyze_channel(acc, fs, channel_index, expected):
    rms_whole = acc.rms()
    rms_first = acc._frame_rms(acc.first_frame)
    rms_last = acc._frame_rms(acc.last_frame)

    cross = crossing_analysis(acc.block, fs)
    freq_meas = cross["freq"]

    nonsteady_empirical = False
    if len(acc.block) >= 8:
        half = len(acc.block) // 2
        a = math.fsum(map(operator.mul, acc.block[:half], acc.block[:half]))
        b = math.fsum(map(operator.mul, acc.block[half:], acc.block[half:]))
        rms_a = math.sqrt(a / half)
        rms_b = math.sqrt(b / (len(acc.block) - half))
        denom = max(rms_a, rms_b)
        if denom > 0.0 and abs(rms_b - rms_a) / denom > EMPIRICAL_RMS_REL:
            nonsteady_empirical = True
    if cross["spread"] is not None and cross["spread"] > EMPIRICAL_FREQ_REL:
        nonsteady_empirical = True

    f_analysis = freq_meas
    if f_analysis is None:
        f_analysis = expected.get("frequency_hz")

    orders = [h["order"] for h in expected.get("harmonics", [])]
    fund_rms = None
    phase = None
    harmonic_rms = {}
    if f_analysis is not None and acc.block:
        fund_rms, phase, harmonic_rms = spectral_measure(
            acc.block, fs, f_analysis, orders)

    harmonic_report = []
    for h in expected.get("harmonics", []):
        order = h["order"]
        rms = harmonic_rms.get(order)
        if rms is not None and fund_rms and fund_rms > 0.0:
            ratio = 100.0 * rms / fund_rms
        else:
            ratio = None
        harmonic_report.append({"order": order, "amp_rel_pct": rr(ratio, 6)})

    return {
        "index": channel_index,
        "name": expected.get("name", CHANNEL_NAMES[channel_index]),
        "sample_count": acc.n,
        "rms": rr(rms_whole),
        "rms_fundamental": rr(fund_rms),
        "rms_first_frame": rr(rms_first),
        "rms_last_frame": rr(rms_last),
        "frequency_hz": rr(freq_meas, 9),
        "frequency_method": "rising_zero_crossing_linear_interp",
        "phase_deg": rr(phase, 6),
        "harmonics": harmonic_report,
        "steady_empirical": not nonsteady_empirical,
        "expected": {
            "nominal": finite(expected.get("nominal")),
            "amp_pct": finite(expected.get("amp_pct")),
            "frequency_hz": finite(expected.get("frequency_hz")),
            "phase_deg": finite(expected.get("phase_deg")),
        },
        "_rms": rms_whole,
        "_freq": freq_meas,
        "_fund_rms": fund_rms,
        "_phase": phase,
        "_harmonic_rms": harmonic_rms,
        "_nonsteady": nonsteady_empirical,
    }


# --------------------------------------------------------------------------
# main verification
# --------------------------------------------------------------------------

def verify(path, manifest_path, expect_path=None):
    checks = Checks()
    result = {
        "file": os.path.abspath(path),
        "measurement_scope": MEASUREMENT_SCOPE,
        "structure": {},
        "channels": [],
        "dynamic_components": [],
        "component_measurements": [],
        "checks": checks.items,
        "summary": "FAIL",
    }
    if manifest_path:
        result["manifest"] = os.path.abspath(manifest_path)
    if expect_path:
        result["expect"] = os.path.abspath(expect_path)

    try:
        size = os.path.getsize(path)
    except OSError as exc:
        checks.add("file.readable", "readable", str(exc), None, "FAIL")
        result["summary"] = "FAIL"
        return result

    min_size = FILE_HDR_BYTES + END_REC_BYTES
    if size < min_size:
        checks.add("file.size", ">= %d" % min_size, size, 0, "FAIL")
        result["summary"] = "FAIL"
        return result

    with open(path, "rb") as fh:
        raw = fh.read(FILE_HDR_BYTES)
        if len(raw) != FILE_HDR_BYTES:
            checks.add("file.header.length", FILE_HDR_BYTES, len(raw), 0, "FAIL")
            result["summary"] = "FAIL"
            return result
        (magic, version, header_bytes, sample_rate, channel_count,
         frame_samples, fmt, hdr_flags, start_utc, sample_period_ns,
         u_scale, i_scale, reserved) = FILE_HDR.unpack(raw)

        checks.ok("file.header.magic", magic == FILE_MAGIC,
                  "WGIEC625", magic.decode("ascii", "replace"))
        checks.ok("file.header.version", version == 1, 1, version)
        checks.ok("file.header.header_bytes", header_bytes == FILE_HDR_BYTES,
                  FILE_HDR_BYTES, header_bytes)
        checks.ok("file.header.sample_rate_hz", sample_rate > 0,
                  "> 0", sample_rate)
        checks.ok("file.header.channel_count", 1 <= channel_count <= 7,
                  "1..7", channel_count)
        fmt_ok = fmt in FORMAT_NAMES
        checks.ok("file.header.format", fmt_ok, "0|1|2", fmt)
        checks.ok("file.header.reserved_zero", reserved == 0, 0, reserved)
        checks.ok("file.header.flags_reserved",
                  (hdr_flags & ~0x1) == 0, "bits 1..15 = 0", hdr_flags)
        if sample_rate > 0:
            ideal_period = 1e9 / sample_rate
            checks.ok("file.header.sample_period_ns",
                      abs(sample_period_ns - ideal_period) <= 1e-6,
                      ideal_period, rr(sample_period_ns), 1e-6, "ns")
        else:
            checks.add("file.header.sample_period_ns", "n/a",
                       rr(sample_period_ns), None, "SKIP")
        if fmt in (0, 1):
            checks.ok("file.header.scales_zero_non_i24",
                      u_scale == 0.0 and i_scale == 0.0, "0 / 0",
                      [rr(u_scale, 12), rr(i_scale, 12)])

        bps = BYTES_PER_SAMPLE.get(fmt)
        frame_meta = []
        errs = []
        end_off = size - END_REC_BYTES
        total_samples = 0
        off = FILE_HDR_BYTES

        block_cap = max(1, min(int(sample_rate * BLOCK_SECONDS) + 2,
                               BLOCK_MAX_SAMPLES))
        accs = [ChannelAcc(block_cap) for _ in range(channel_count)]

        first_flags_ok = True
        last_flags_ok = True
        index_ok = True
        sample_seq_ok = True
        ts_start_ok = True
        ts_end_ok = True
        ts_cont_ok = True
        payload_ok = True
        ch_count_ok = True
        frame_count_ok = True
        reserved_ok = True
        frame_hdr_ok = True
        expected_sample = 0
        prev_end = None

        while off < end_off:
            if end_off - off < FRAME_HDR_BYTES:
                errs.append("truncated frame header at offset %d" % off)
                frame_hdr_ok = False
                break
            fh.seek(off)
            raw = fh.read(FRAME_HDR_BYTES)
            if len(raw) != FRAME_HDR_BYTES:
                errs.append("short frame header at offset %d" % off)
                frame_hdr_ok = False
                break
            (fmagic, fversion, fheader_bytes, frame_index, start_sample,
             frame_start_utc, frame_end_utc, frames, flags, fch,
             freserved, payload_bytes) = FRAME_HDR.unpack(raw)
            off += FRAME_HDR_BYTES

            if fmagic != FRAME_MAGIC:
                errs.append("frame %d magic %r" % (len(frame_meta), fmagic))
                frame_hdr_ok = False
            if fversion != 1:
                errs.append("frame %d version %d" % (len(frame_meta), fversion))
                frame_hdr_ok = False
            if fheader_bytes != FRAME_HDR_BYTES:
                errs.append("frame %d header_bytes %d"
                            % (len(frame_meta), fheader_bytes))
                frame_hdr_ok = False
            if freserved != 0:
                errs.append("frame %d reserved %d"
                            % (len(frame_meta), freserved))
                reserved_ok = False
            if (flags & ~0x7) != 0:
                errs.append("frame %d flags 0x%x" % (len(frame_meta), flags))
                reserved_ok = False
            if frames == 0:
                errs.append("frame %d frames 0" % len(frame_meta))
                frame_count_ok = False
            if not (0 < frames <= frame_samples):
                errs.append("frame %d frames %d > frame_samples %d"
                            % (len(frame_meta), frames, frame_samples))
                frame_count_ok = False
            if fch != channel_count:
                errs.append("frame %d channel_count %d != %d"
                            % (len(frame_meta), fch, channel_count))
                ch_count_ok = False
            if frame_index != len(frame_meta):
                errs.append("frame %d index %d" % (len(frame_meta), frame_index))
                index_ok = False
            if start_sample != expected_sample:
                errs.append("frame %d start_sample %d != %d"
                            % (len(frame_meta), start_sample, expected_sample))
                sample_seq_ok = False
            exp_start_utc = start_utc + ns_at(start_sample, sample_rate)
            exp_end_utc = start_utc + ns_at(start_sample + frames, sample_rate)
            if frame_start_utc != exp_start_utc:
                errs.append("frame %d start_utc %d != %d"
                            % (len(frame_meta), frame_start_utc, exp_start_utc))
                ts_start_ok = False
            if frame_end_utc != exp_end_utc:
                errs.append("frame %d end_utc %d != %d"
                            % (len(frame_meta), frame_end_utc, exp_end_utc))
                ts_end_ok = False
            if prev_end is not None and frame_start_utc != prev_end:
                errs.append("frame %d start_utc %d != prev end %d"
                            % (len(frame_meta), frame_start_utc, prev_end))
                ts_cont_ok = False
            prev_end = frame_end_utc
            if len(frame_meta) == 0 and not (flags & 0x2):
                first_flags_ok = False
            if len(frame_meta) > 0 and (flags & 0x2):
                first_flags_ok = False
            expected_payload = frames * channel_count * (bps or 0)
            if payload_bytes != expected_payload:
                errs.append("frame %d payload_bytes %d != %d"
                            % (len(frame_meta), payload_bytes, expected_payload))
                payload_ok = False
            if off + payload_bytes > end_off:
                errs.append("frame %d payload crosses end record" % len(frame_meta))
                payload_ok = False
                break

            payload = fh.read(payload_bytes)
            if len(payload) != payload_bytes:
                errs.append("frame %d short payload" % len(frame_meta))
                payload_ok = False
                break
            off += payload_bytes

            if bps:
                plane = frames * bps
                for ch in range(channel_count):
                    chunk = payload[ch * plane:(ch + 1) * plane]
                    if fmt == 2:
                        scale = u_scale if ch < 3 else i_scale
                    else:
                        scale = 1.0
                    vals = decode_samples(chunk, frames, fmt, scale)
                    accs[ch].add(vals)

            frame_meta.append({
                "frame_index": frame_index,
                "start_sample": start_sample,
                "start_utc_ns": frame_start_utc,
                "end_utc_ns": frame_end_utc,
                "frames": frames,
                "flags": flags,
            })
            expected_sample += frames
            total_samples += frames

        # end record
        fh.seek(end_off)
        raw_end = fh.read(END_REC_BYTES)
        if len(raw_end) == END_REC_BYTES:
            (emagic, eversion, reason, end_frames, end_samples,
             end_reserved) = END_REC.unpack(raw_end)
        else:
            emagic = eversion = reason = end_frames = end_samples = end_reserved = None
        checks.ok("file.end.magic", emagic == END_MAGIC,
                  "WGE1", None if emagic is None else emagic.decode("ascii", "replace"))
        checks.ok("file.end.version", eversion == 1, 1, eversion)
        checks.ok("file.end.reason", reason in (0, 1, 2), "0|1|2", reason)
        checks.ok("file.end.reserved_zero", end_reserved == 0, 0, end_reserved)
        checks.ok("file.end.total_frames", end_frames == len(frame_meta),
                  len(frame_meta), end_frames)
        checks.ok("file.end.total_samples", end_samples == total_samples,
                  total_samples, end_samples)
        checks.ok("file.end.position", off == end_off, end_off, off)

        # frame level aggregates
        checks.ok("frame.header_fields", frame_hdr_ok, "magic/version/header_bytes",
                  "ok" if frame_hdr_ok else "; ".join(errs[:5]))
        checks.ok("frame.payload_bytes", payload_ok,
                  "frames*channel_count*bytes_per_sample",
                  "ok" if payload_ok else "; ".join(errs[:5]))
        checks.ok("frame.channel_count", ch_count_ok, channel_count,
                  "ok" if ch_count_ok else "mismatch")
        checks.ok("frame.index_sequence", index_ok, "0..n-1",
                  "ok" if index_ok else "mismatch")
        checks.ok("frame.start_sample_sequence", sample_seq_ok,
                  "cumulative", "ok" if sample_seq_ok else "mismatch")
        checks.ok("frame.count", frame_count_ok and bool(frame_meta),
                  ">= 1 and 0 < frames <= frame_samples",
                  len(frame_meta))
        checks.ok("frame.reserved_bits", reserved_ok, 0,
                  "ok" if reserved_ok else "non-zero")
        checks.ok("frame.flags.first", first_flags_ok,
                  "FIRST on frame 0 only", first_flags_ok)
        last_flags_ok = bool(frame_meta) and bool(frame_meta[-1]["flags"] & 0x4)
        checks.ok("frame.flags.last", last_flags_ok,
                  "LAST on final frame", last_flags_ok)
        layout_ok = all(m["frames"] == frame_samples
                        for m in frame_meta[:-1]) if frame_meta else False
        checks.ok("frame.samples_layout", layout_ok,
                  "all frames except last == frame_samples", layout_ok)
        checks.ok("timestamp.start_formula", ts_start_ok,
                  "header_start + ns_at(start_sample)", "exact",
                  TOL_TIME_NS, "ns")
        checks.ok("timestamp.end_formula", ts_end_ok,
                  "header_start + ns_at(start_sample+frames)", "exact",
                  TOL_TIME_NS, "ns")
        checks.ok("timestamp.continuity", ts_cont_ok,
                  "frame[k].end == frame[k+1].start", "exact",
                  TOL_TIME_NS, "ns")

        if fmt == 2:
            present_u = any(i < 3 for i in range(channel_count))
            present_i = any(i >= 3 for i in range(channel_count))
            checks.ok("file.header.u_scale", (not present_u) or u_scale > 0.0,
                      "> 0 when voltage channels present", u_scale)
            checks.ok("file.header.i_scale", (not present_i) or i_scale > 0.0,
                      "> 0 when current channels present", i_scale)

    timestamp = {}
    if frame_meta and total_samples > 0:
        measured_slope = ((frame_meta[-1]["end_utc_ns"]
                           - frame_meta[0]["start_utc_ns"]) / total_samples)
        ideal_slope = 1e9 / sample_rate
        max_dev = 0.0
        if len(frame_meta) >= 1:
            for m in frame_meta:
                s = ((m["end_utc_ns"] - m["start_utc_ns"]) / m["frames"])
                max_dev = max(max_dev, abs(s - ideal_slope))
        checks.ok("timestamp.slope", abs(measured_slope - ideal_slope) <= 1e-3,
                  rr(ideal_slope), rr(measured_slope, 12), 1e-3, "ns/sample")
        timestamp = {
            "ideal_ns_per_sample": rr(ideal_slope),
            "measured_ns_per_sample": rr(measured_slope, 12),
            "max_frame_slope_deviation_ns": rr(max_dev, 9),
        }
    else:
        checks.add("timestamp.slope", "n/a", None, None, "SKIP")
        measured_slope = None

    structure_ok = all(c["status"] == "PASS" for c in checks.items)

    result["structure"] = {
        "ok": structure_ok,
        "file_size": size,
        "header": {
            "magic": magic.decode("ascii", "replace"),
            "version": version,
            "header_bytes": header_bytes,
            "sample_rate_hz": sample_rate,
            "channel_count": channel_count,
            "frame_samples": frame_samples,
            "format": FORMAT_NAMES.get(fmt, str(fmt)),
            "format_id": fmt,
            "flags": hdr_flags,
            "start_utc_ns": start_utc,
            "sample_period_ns": rr(sample_period_ns),
            "u_scale": rr(u_scale, 12),
            "i_scale": rr(i_scale, 12),
            "reserved": reserved,
        },
        "frames": {
            "count": len(frame_meta),
            "total_samples": total_samples,
            "first_start_utc_ns": frame_meta[0]["start_utc_ns"] if frame_meta else None,
            "last_end_utc_ns": frame_meta[-1]["end_utc_ns"] if frame_meta else None,
            "errors": errs[:20],
        },
        "end": {
            "magic": None if emagic is None else emagic.decode("ascii", "replace"),
            "version": eversion,
            "reason": reason,
            "total_frames": end_frames,
            "total_samples": end_samples,
            "reserved": end_reserved,
        },
        "timestamp": timestamp,
    }

    if not structure_ok:
        result["summary"] = "FAIL"
        return result

    # ------------------------------------------------------------------
    # manifest / expectation based measurements
    # ------------------------------------------------------------------
    info = None
    if manifest_path:
        try:
            info = read_manifest(manifest_path, channel_count)
        except (OSError, ValueError) as exc:
            checks.add("manifest.readable", "valid JSON", str(exc), None, "FAIL")
            result["summary"] = "FAIL"
            return result

    if expect_path:
        try:
            expected_doc = read_expectations(expect_path, channel_count)
        except (OSError, ValueError) as exc:
            checks.add("expect.readable", "valid JSON", str(exc), None, "FAIL")
            result["summary"] = "FAIL"
            return result
        info = merge_info(info, expected_doc)

    if info is None:
        result["summary"] = checks.status()
        return result

    if info["sample_rate_hz"] is not None:
        checks.ok("manifest.sample_rate_hz",
                  sample_rate == info["sample_rate_hz"],
                  info["sample_rate_hz"], sample_rate)
    else:
        checks.add("manifest.sample_rate_hz", "present", None, None, "SKIP")
    if info["start_utc_ns"] is not None:
        checks.ok("manifest.start_utc_ns",
                  start_utc == info["start_utc_ns"],
                  info["start_utc_ns"], start_utc)
    else:
        checks.add("manifest.start_utc_ns", "present", None, None, "SKIP")
    if info["sample_count"] is not None:
        checks.ok("manifest.sample_count",
                  total_samples == info["sample_count"],
                  info["sample_count"], total_samples)
    else:
        checks.add("manifest.sample_count", "present", None, None, "SKIP")
    if info["format"] is not None:
        checks.ok("manifest.format",
                  str(info["format"]).lower() == FORMAT_NAMES.get(fmt, "?"),
                  info["format"], FORMAT_NAMES.get(fmt, "?"))
    else:
        checks.add("manifest.format", "present", None, None, "SKIP")
    if info["frame_samples"] is not None:
        checks.ok("manifest.frame_samples",
                  frame_samples == info["frame_samples"],
                  info["frame_samples"], frame_samples)
    else:
        checks.add("manifest.frame_samples", "present", None, None, "SKIP")
    if info["duration_s"] is not None:
        expected_count = half_up(info["duration_s"] * sample_rate)
        checks.ok("sample_count.duration",
                  total_samples == expected_count,
                  expected_count, total_samples, 0, "samples")
    else:
        checks.add("sample_count.duration", "duration_s present", None, None, "SKIP")

    nonsteady = bool(info["nonsteady_hint"])
    components = info.get("components") or []
    events = info.get("events") or []
    frequency_dynamic = bool(info.get("frequency_dynamic"))
    base_freq = finite(info.get("frequency_hz"))

    dynamic_components = []
    for comp in components:
        if not comp.get("dynamic"):
            continue
        if comp.get("modulators"):
            reason = "modulators: " + ", ".join(comp["modulators"])
        elif frequency_dynamic:
            reason = "global frequency profile"
        elif events and comp["kind"] != "tone":
            reason = "time events"
        elif comp.get("dynamic_own"):
            reason = "legacy amplitude/frequency profile or modulation"
        else:
            reason = "amplitude follows the modulated fundamental"
        dynamic_components.append({
            "index": comp.get("index"),
            "kind": comp["kind"],
            "tag": component_tag(comp),
            "order": comp.get("order"),
            "frequency_hz": comp.get("frequency_hz"),
            "channels": comp.get("channels"),
            "modulators": comp.get("modulators"),
            "reason": reason,
        })
    result["dynamic_components"] = dynamic_components

    fundamental = None
    for comp in components:
        if comp["kind"] == "fundamental":
            fundamental = comp
            break

    # Sidebands of a lower-frequency amplitude-modulated component leak into
    # the DFT of higher components.  Mark such components as measured-only.
    amp_mod_centres = []
    for comp in components:
        if not comp.get("amp_dynamic"):
            continue
        centre = component_frequency(comp, base_freq)
        if centre is not None:
            amp_mod_centres.append(centre)

    channels = []
    for ch in range(channel_count):
        name = info["channels"][ch] if ch < len(info["channels"]) \
            else CHANNEL_NAMES[ch]
        nominal = finite(info["nominal"][ch])
        fund_amp = fundamental["amp_pct"].get(name, 0.0) if fundamental else None
        fund_phase = fundamental["phase_deg"].get(name, 0.0) if fundamental \
            else (info["phase_deg"][ch] if ch < len(info["phase_deg"]) else None)
        fund_rms = (nominal * fund_amp / 100.0
                    if nominal is not None and fund_amp is not None else None)
        harmonics_expected = [
            {"order": c["order"], "amp_pct": c["amp_pct"].get(name, 0.0)}
            for c in components
            if c["kind"] == "harmonic" and c["order"] is not None
            and c["amp_pct"].get(name, 0.0) > 0.0
        ]
        expected = {
            "name": name,
            "nominal": nominal,
            "amp_pct": fund_amp,
            "frequency_hz": base_freq,
            "phase_deg": fund_phase,
            "harmonics": harmonics_expected,
        }
        data = analyze_channel(accs[ch], sample_rate, ch, expected)

        dynamic_here = any(
            c.get("dynamic") and name in c.get("channels", [])
            and c["amp_pct"].get(name, 0.0) > 0.0 for c in components)
        channel_dynamic = (dynamic_here or frequency_dynamic or bool(events)
                           or nonsteady)
        steady = (not channel_dynamic) and data["_nonsteady"] is False
        data["steady"] = steady
        data["dynamic"] = channel_dynamic
        channels.append((data, expected, steady, name, nominal, fund_rms))

    result["channels"] = []
    for data, expected, steady, name, nominal, fund_rms in channels:
        public = dict(data)
        for key in ("_rms", "_freq", "_fund_rms", "_phase",
                    "_harmonic_rms", "_nonsteady"):
            public.pop(key, None)
        result["channels"].append(public)

    component_measurements = []
    for ch, (data, expected, steady, name, nominal, fund_rms) in \
            enumerate(channels):
        prefix = "channel.%s" % name
        block = accs[ch].block
        dyn_note = "DYNAMIC: measured only, not judged"

        # ---- channel summary ---------------------------------------
        if not steady:
            checks.add(prefix + ".rms", finite(nominal), data["rms"],
                       TOL_RMS_REL, "SKIP", "V/A", dyn_note)
            checks.add(prefix + ".frequency_hz", base_freq,
                       data["frequency_hz"], TOL_FREQ_HZ, "SKIP", "Hz",
                       dyn_note)
            checks.add(prefix + ".phase_deg", finite(expected.get("phase_deg")),
                       data["phase_deg"], TOL_PHASE_DEG, "SKIP", "deg",
                       dyn_note)
        else:
            exp_total_sq = 0.0
            have_total = True
            for comp in components:
                if name not in comp.get("channels", []):
                    continue
                amp = comp["amp_pct"].get(name, 0.0)
                if amp <= 0.0:
                    continue
                exp_rms = component_expected_rms(comp, name, nominal, fund_rms)
                if exp_rms is None:
                    have_total = False
                else:
                    exp_total_sq += exp_rms * exp_rms
            exp_total = math.sqrt(exp_total_sq) if have_total else None
            if exp_total is not None and data["_rms"] is not None:
                rel = abs(data["_rms"] - exp_total) / exp_total \
                    if exp_total > 0.0 else abs(data["_rms"] - exp_total)
                checks.ok(prefix + ".rms", rel <= TOL_RMS_REL,
                          rr(exp_total), rr(data["_rms"]), TOL_RMS_REL,
                          "V/A", "math total RMS, relative error %.3g" % rel)
            else:
                checks.add(prefix + ".rms", "analytic total", data["rms"],
                           TOL_RMS_REL, "SKIP", "V/A")

            if base_freq is not None and data["_freq"] is not None:
                checks.ok(prefix + ".frequency_hz",
                          abs(data["_freq"] - base_freq) <= TOL_FREQ_HZ,
                          base_freq, rr(data["_freq"], 9), TOL_FREQ_HZ, "Hz",
                          "zero-crossing vs manifest frequency")
            else:
                checks.add(prefix + ".frequency_hz", base_freq,
                           data["frequency_hz"], TOL_FREQ_HZ, "SKIP", "Hz")

            exp_phase = finite(expected.get("phase_deg"))
            if exp_phase is not None and data["_phase"] is not None:
                diff = wrap_diff(data["_phase"], exp_phase)
                checks.ok(prefix + ".phase_deg", abs(diff) <= TOL_PHASE_DEG,
                          exp_phase, rr(data["_phase"], 6), TOL_PHASE_DEG,
                          "deg", "wrapped difference %.6g" % diff)
            else:
                checks.add(prefix + ".phase_deg", exp_phase,
                           data["phase_deg"], TOL_PHASE_DEG, "SKIP", "deg")

        # ---- per-component analytic reconstruction -----------------
        for comp in components:
            if name not in comp.get("channels", []):
                continue
            amp = comp["amp_pct"].get(name, 0.0)
            if amp <= 0.0:
                continue
            tag = component_tag(comp)
            label = "%s.component.%s" % (prefix, tag)
            target = component_frequency(comp, base_freq)
            measured_rms, measured_phase = tone_measure(block, sample_rate,
                                                        target)
            exp_rms = component_expected_rms(comp, name, nominal, fund_rms)
            exp_phase = comp["phase_deg"].get(name, 0.0)
            tol = TOL_RMS_REL if comp["kind"] == "fundamental" \
                else TOL_HARM_REL
            contaminated = (target is not None
                            and any(c < target for c in amp_mod_centres))
            status = "SKIP" if (comp.get("dynamic") or contaminated) else None
            note = None
            if comp.get("dynamic"):
                note = "DYNAMIC: " + (", ".join(comp["modulators"])
                                      if comp.get("modulators")
                                      else "time-varying component")
            elif contaminated:
                note = ("measurement contaminated by lower-frequency "
                        "amplitude modulation; measured only")
            else:
                note = "analytic reconstruction from manifest"

            report_rms = None if comp.get("dynamic") else rr(exp_rms)
            report_phase = None if comp.get("dynamic") else rr(exp_phase)
            if measured_rms is None:
                checks.add(label + ".rms", report_rms, None, tol,
                           status or "SKIP", "V/A", note)
            elif exp_rms is not None and exp_rms > 0.0:
                rel = abs(measured_rms - exp_rms) / exp_rms
                checks.ok(label + ".rms", rel <= tol, report_rms,
                          rr(measured_rms), tol, "V/A",
                          (note or "") + "; relative error %.3g" % rel)
                if status == "SKIP":
                    checks.items[-1]["status"] = "SKIP"
            else:
                checks.add(label + ".rms", report_rms, rr(measured_rms),
                           tol, status or "SKIP", "V/A", note)

            checks.add(label + ".phase_deg", report_phase,
                       rr(measured_phase, 6), TOL_PHASE_DEG,
                       status or ("SKIP" if measured_phase is None else None)
                       or "PASS",
                       "deg", note)
            if status is None:
                last = checks.items[-1]
                if measured_phase is None:
                    last["status"] = "SKIP"
                else:
                    diff = wrap_diff(measured_phase, exp_phase)
                    last["status"] = ("PASS" if abs(diff) <= TOL_PHASE_DEG
                                      else "FAIL")
                    last["note"] = "wrapped difference %.6g deg" % diff

            component_measurements.append({
                "channel": name,
                "component": tag,
                "kind": comp["kind"],
                "dynamic": bool(comp.get("dynamic")),
                "frequency_hz": rr(target),
                "expected_rms": report_rms,
                "measured_rms": rr(measured_rms),
                "expected_phase_deg": report_phase,
                "measured_phase_deg": rr(measured_phase, 6),
                "status": "SKIP" if (comp.get("dynamic") or contaminated)
                          else ("PASS" if (measured_rms is not None)
                                else "SKIP"),
            })

    result["measurement_scope"] = MEASUREMENT_SCOPE
    result["component_measurements"] = component_measurements
    result["summary"] = checks.status()
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Independent mathematical verifier for waveform files (v1).")
    parser.add_argument("waveform", help="waveform .bin file")
    parser.add_argument("--manifest", help="manifest JSON (default: <bin>.manifest.json)")
    parser.add_argument("--expect",
                        help="expected-signal JSON (default: <bin>.expect.json)")
    parser.add_argument("--report", help="write the JSON report to this path")
    args = parser.parse_args(argv)

    manifest_path = args.manifest
    if manifest_path is not None:
        if not os.path.isfile(manifest_path):
            sys.stderr.write("manifest not found: %s\n" % manifest_path)
            return 3
    else:
        manifest_path = find_manifest(args.waveform)

    expect_path = args.expect
    if expect_path is not None:
        if not os.path.isfile(expect_path):
            sys.stderr.write("expectation file not found: %s\n" % expect_path)
            return 3
    else:
        expect_path = find_expectations(args.waveform)

    try:
        result = verify(args.waveform, manifest_path, expect_path)
    except OSError as exc:
        sys.stderr.write("cannot read waveform: %s\n" % exc)
        return 3

    text = json.dumps(result, ensure_ascii=True, indent=2, allow_nan=False)
    sys.stdout.write(text + "\n")
    if args.report:
        try:
            with open(args.report, "w", encoding="utf-8") as fh:
                fh.write(text + "\n")
        except OSError as exc:
            sys.stderr.write("cannot write report: %s\n" % exc)
            return 3

    return {"PASS": 0, "FAIL": 1, "PARTIAL": 2}.get(result["summary"], 1)


if __name__ == "__main__":
    sys.exit(main())
