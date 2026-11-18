# WaveForge User Manual

> Version 1.0.0 (`WaveForge 1.0.0`)
> Platforms: Linux, WSL and macOS are the primary targets. Native Windows builds work for the CLI; the self-test assumes POSIX facilities.
> This manual explains usage. For contracts and internals see [Architecture](architecture.md), [Input format](input-format.md) and [Output format](output-format.md).

## 1. What WaveForge is

WaveForge is a C99 software waveform generator:

- **Input**: a scenario name plus overrides, a single-level JSON document, or the C API.
- **Output**: a multi-channel waveform file (`WGIEC625` layout) or a frame-by-frame callback stream.
- **Purpose**: provide reproducible test stimuli for power-quality instruments, algorithms and replay systems.
- **Out of scope**: ADC, analog front end, anti-aliasing, real-time clocks and any IEC conformity verdict.

## 2. Build and install

### 2.1 Requirements

| Item | Notes |
|---|---|
| C99 compiler | gcc, clang or MSVC; `make` uses `cc` by default |
| GNU Make | or use the `tools/build.sh` wrapper |
| Python 3.8+ | only for the independent verifiers (standard library only) |
| `sha256sum` | only for the determinism self-test (Linux/WSL/macOS) |

The runtime needs only the C standard library and `libm`.

### 2.2 Make

```sh
make            # builds build/libwaveforge.a, build/wg and build/selftest
make clean
make test
```

| Artifact | Description |
|---|---|
| `build/libwaveforge.a` | static library (Signal Core + Preset Layer) |
| `build/wg` | command-line generator |
| `build/selftest` | 13 self-tests |

`sh tools/build.sh` cleans, rebuilds and runs the self-test.

> Build outputs live in `build/` and are gitignored; do not commit binaries.

### 2.3 CMake

The repository ships a `CMakeLists.txt`:

```sh
cmake -S . -B build/cmake          # separate from the make build/
cmake --build build/cmake
ctest --test-dir build/cmake --output-on-failure
```

| Target | Output |
|---|---|
| `waveforge` | static library `libwaveforge.a` |
| `wg` | command-line generator |
| `selftest` | self-test (registered with CTest) |

### 2.4 Examples

`examples/` contains ready-to-use inputs:

| File | Content |
|---|---|
| `examples/basic.json` | basic steady-state configuration |
| `examples/harmonics.json` | harmonic configuration |
| `examples/multi_modulation.json` | combined modulation |
| `examples/builder_example.c` | builder C API example |

```sh
./build/wg gen --config examples/harmonics.json --out h.bin

cc -std=c99 -Iinclude examples/builder_example.c build/libwaveforge.a -lm \
   -o build/builder_example
./build/builder_example /tmp/builder_example.bin
```

### 2.5 Windows notes

- The generator itself is portable C99.
- `selftest` uses `unistd.h`, `/proc/self/statm` and `sha256sum`; run it under WSL/Linux.
- Under native Windows, use `wg` plus the Python verifiers.

## 3. Quick start

```sh
make
./build/wg list
./build/wg gen --test A1.2.2 --system 50 --duration 60 --out a122.bin
python3 tools/verify.py a122.bin --report a122.verify.json
cat a122.bin.manifest.json
```

## 4. `wg list`

```text
A1.2.2        50/60  steady 50.05 Hz (60 Hz 59.95 Hz), 60 s
A4.1.3        50/60  threshold-margin step, no default delta (NO_DEFAULT)
A6.1.6        50/60  EXCLUDED: anti-alias proof belongs to analog front end
```

Columns: scenario id (case-insensitive), applicable system frequency, description. Non-default entries carry a `CUSTOM:`, `NO_DEFAULT` or `EXCLUDED:` prefix. Full statuses are in [Signal catalog](signal-catalog.md).

## 5. One-line generation

```sh
./build/wg gen --test A1.2.1 --out a121.bin
./build/wg gen --test A1.2.2 --out a122.bin
./build/wg gen --test A1.2.3 --out a123.bin
./build/wg gen --test A1.1.1 --duration 2min --out a111.bin
./build/wg gen --test A2.1.1 --out a211.bin
./build/wg gen --test A6.2.2 --duration 2 --out h3.bin
./build/wg gen --test A8.1.3 --duration 2 --out carrier.bin
./build/wg gen --test A4.2.1 --duration 15 --out a421.bin
```

### Overrides

Only the fields you set are replaced; everything else keeps the preset default.

```sh
./build/wg gen --test A6.5.1 --duration 30 --out h3ramp.bin
./build/wg gen --test A1.2.2 --freq 49.95 --out a122_4995.bin
./build/wg gen --test A2.2.2 --amp Ua=80%,Ub=80%,Uc=80% --phase 0,-120,120 --out a222.bin
./build/wg gen --test A1.2.2 --duration 2 --harmonics "3=10%,180" --interharmonics "1.5=5%,0" --out mix.bin
./build/wg gen --test A1.2.2 --duration 5 --mod sine:2.3:10:0 --out am.bin
./build/wg gen --test A1.2.2 --duration 10 --phase-step 5.0:30 --out step.bin
./build/wg gen --test A1.2.2 --system 60 --out a122_60.bin
```

Amplitude values may carry `%` (relative to the channel nominal) or `V`/`A` (absolute RMS). Without a unit, the channel's physical unit is assumed.

`--mod` accepts `sine:FM:DEPTH[:PHASE]` and `rect:FM:DEPTH:DUTY[:PHASE]`. For linear/triangle modulation, frequency ramps and events, use JSON.

## 6. JSON programs

```json
{
  "time": { "duration_s": 10, "sample_rate_hz": 51200 },
  "channels": ["Ua", "Ub", "Uc"],
  "program": {
    "components": [
      {
        "kind": "fundamental",
        "frequency_hz": 50,
        "amp_pct": 100,
        "phase_deg": { "Ua": 0, "Ub": -120, "Uc": 120 },
        "modulators": [
          { "kind": "amp_sine", "frequency_hz": 2.3, "depth_pct": 10 },
          { "kind": "freq_triangle", "from": 40, "to": 60, "period_s": 6 }
        ]
      },
      { "kind": "harmonic", "order": 3, "amp_pct": 7, "phase_deg": 45 },
      { "kind": "interharmonic", "order": 1.5, "amp_pct": 5 },
      { "kind": "tone", "frequency_hz": 1060, "amp_pct": 1 }
    ],
    "events": [
      { "kind": "phase_step", "time_s": 5, "channel_mask": "Ua,Ub,Uc", "value": 30 }
    ]
  },
  "output": { "format": "f64", "frame_samples": 256 }
}
```

```sh
./build/wg gen --config my.json --out custom.bin
```

Notes:

- `program.components` takes precedence over legacy `signal.harmonics/interharmonics/tones/modulation`.
- `frequency_hz` is required for `tone`; it can be omitted for the other kinds (derived from `order`).
- `period_s` is a full round trip: 40→60→40 Hz uses `6`; a 3 s one-way ramp also uses `6`.
- `phase_step` adds to the integrated phase; harmonic/interharmonic components scale it by `order` (H3 gets +90°), while `tone` is unaffected.
- Event times must lie in `[0, duration_s]`.

See [Input format](input-format.md) for every field and limit.

## 7. C API

### 7.1 Headers and linking

```c
#include "wg.h"          /* public API */
#include "wg_config.h"   /* optional: defaults, JSON, scenarios */
```

```sh
cc -std=c99 -O2 -Iinclude my_tool.c build/libwaveforge.a -lm -o my_tool
```

### 7.2 Builder

```c
#include <string.h>
#include "wg.h"

int main(void)
{
    const uint8_t ch[3] = { WG_CH_UA, WG_CH_UB, WG_CH_UC };
    const double  nom[3] = { 220.0, 220.0, 220.0 };
    wg_builder_t *b = wg_builder_create(51200u, 0u);
    wg_component_t c;
    wg_modulator_t m;
    wg_handle_t *h;
    char err[256];

    if (b == NULL) return 1;
    wg_builder_set_channels(b, ch, 3u, nom);
    wg_builder_set_duration(b, 10.0);
    wg_builder_set_output(b, (uint8_t)WG_FMT_F64, 256u);

    memset(&c, 0, sizeof(c));
    c.kind = WG_COMP_FUNDAMENTAL;
    c.order = 1.0;
    c.frequency_hz = 50.0;
    c.channel_mask = 0x07u;
    c.amp_pct[WG_CH_UA] = c.amp_pct[WG_CH_UB] = c.amp_pct[WG_CH_UC] = 100.0;
    c.phase_deg[WG_CH_UA] = 0.0;
    c.phase_deg[WG_CH_UB] = -120.0;
    c.phase_deg[WG_CH_UC] = 120.0;
    wg_builder_add_component(b, &c);

    memset(&c, 0, sizeof(c));
    c.kind = WG_COMP_HARMONIC;
    c.order = 3.0;
    c.channel_mask = 0x07u;
    c.amplitude_ref = 1u;
    c.amp_pct[WG_CH_UA] = c.amp_pct[WG_CH_UB] = c.amp_pct[WG_CH_UC] = 7.0;
    c.phase_deg[WG_CH_UA] = c.phase_deg[WG_CH_UB] = c.phase_deg[WG_CH_UC] = 45.0;
    wg_builder_add_component(b, &c);

    memset(&m, 0, sizeof(m));
    m.kind = WG_MOD2_AMP_SINE;
    m.frequency_hz = 2.3;
    m.depth_pct = 10.0;
    wg_builder_add_modulator(b, 0u, &m);

    h = wg_create_from_builder(b, err, sizeof(err));
    wg_builder_destroy(b);          /* safe once the handle exists */
    if (h == NULL) return 1;
    wg_run_file(h, "custom.bin");
    wg_destroy(h);
    return 0;
}
```

### 7.3 File output

```c
int32_t rc = wg_run_file(h, "out.bin");
```

Returns 0 on success or a negative value (-1 argument, -2 I/O, -3 memory, -4 configuration). Existing files are overwritten. On success a sibling `out.bin.manifest.json` is written. A handle may be reused; each run restarts at t = 0.

### 7.4 Stream output

```c
#include <stdio.h>
#include "wg.h"

static int32_t on_frame(const wg_frame_t *f, void *ctx)
{
    (void)ctx;
    printf("frame %llu: %u samples/ch, t=%llu ns\n",
           (unsigned long long)f->frame_index, (unsigned)f->frames,
           (unsigned long long)f->start_utc_ns);
    /* f->samples is valid only during the callback */
    return 0;   /* non-zero aborts and becomes the return value */
}

int main(void)
{
    wg_config_t cfg;
    char err[256];
    wg_handle_t *h;

    wg_config_default(&cfg);
    cfg.duration_s = 3600.0;
    cfg.frame_samples = 512u;
    cfg.start_utc_ns = 1794700800000000000ull;

    h = wg_create(&cfg, err, sizeof(err));
    if (h == NULL) { fprintf(stderr, "%s\n", err); return 1; }
    wg_run_stream(h, on_frame, NULL);
    wg_destroy(h);
    return 0;
}
```

Streaming writes no files and no manifest. Frames are byte-identical to the file payload.

## 8. Reading the output

- 64-byte file header (`WGIEC625`).
- Repeated frames: 56-byte header (`WGF1`) plus planar samples.
- 32-byte end record (`WGE1`).

Offsets, i24 scaling and timestamp formulas are in [Output format](output-format.md). In short:

1. Read 64 bytes, check the magic and `format`.
2. Read 56-byte frame headers until the end record; use `payload_bytes`.
3. For i24, restore values with `u_scale` / `i_scale`.
4. Recompute timestamps from `start_sample` and the sample rate; do not accumulate.

`<output>.manifest.json` records the effective parameters and the waveform SHA-256.

## 9. Independent verification

Both tools read only the public file format and never link the generator library.

### `verify.py`

```sh
python3 tools/verify.py a122.bin
python3 tools/verify.py a122.bin --report a122.verify.json
python3 tools/verify.py a122.bin --manifest a122.bin.manifest.json
```

It checks structure, timestamps and steady-state RMS/frequency/phase. Components with modulators, frequency profiles or events are marked `DYNAMIC` and excluded from steady single-value checks. Exit codes: 0 PASS, 1 FAIL, 2 PARTIAL, 3 usage/I/O error. Without a manifest it still validates structure.

### `check_acceptance.py`

```sh
python3 tools/check_acceptance.py rms  a122.bin --channel Ua --window 0.2
python3 tools/check_acceptance.py freq a122.bin --channel Ua --window 1
python3 tools/check_acceptance.py dft  a122.bin --channel Ua --freq 150
python3 tools/check_acceptance.py a1-1-1 a111.bin
python3 tools/check_acceptance.py a2-5-1 a251.bin
python3 tools/check_acceptance.py a4-2-1 a421.bin
python3 tools/check_acceptance.py a5-1-2 a512.bin
python3 tools/check_acceptance.py a6-2-2 a622.bin
python3 tools/check_acceptance.py a6-5-1 a651.bin
python3 tools/check_acceptance.py a8-1-3 a813.bin
```

Output is JSON (`checks` plus raw `values`). Exit code 0 = PASS, 1 = FAIL.

### Self-test

```sh
make test
```

Thirteen tests cover configuration validation, stream counters/timestamps, file-vs-stream identity, determinism, phase continuity, i24 encoding, a 660 s streaming run, arbitrary builder content, per-component envelopes, stacked modulation, custom triangles, `NO_DEFAULT`, and legacy compatibility. See [Verification](verification.md).

## 10. Common recipes

### Unbalanced three-phase

```sh
./build/wg gen --test A5.1.2 --out a512.bin
./build/wg gen --test A5.1.1 --amp Ua=73%,Ub=80%,Uc=87% --out unbal.bin
```

### Per-phase dip with zero-crossing switching

Use `segments`; the sum of segment durations must equal `time.duration_s`.

```json
{
  "time": { "duration_s": 1.0 },
  "segments": [
    { "duration_s": 0.2, "signal": { "amp_pct": 100 } },
    {
      "duration_s": 0.2,
      "transition": "zero_crossing",
      "channel_delay_s": { "Ua": 0, "Ub": 0.00333, "Uc": 0.00667 },
      "signal": { "amp_pct": 0 }
    },
    { "duration_s": 0.6, "signal": { "amp_pct": 100 } }
  ]
}
```

### Harmonic ramp

```sh
./build/wg gen --test A6.5.1 --duration 30 --out h3ramp.bin
```

The preset uses an `amp_triangle` on H3 from 10 % down to 0 and back (1 %/s).

### Long runs

- File mode: plan disk space (f64, 3 channels, 2 h ≈ 8.8 GB).
- Stream mode: constant memory, suitable for multi-hour output.

### Custom rate and sample format

```sh
./build/wg gen --test A1.2.2 --rate 25600 --frame 512 --format i24 --out a122_i24.bin
```

For i24, always read the header scales; never assume 220 V.

## 11. Troubleshooting

| Symptom | Cause | Action |
|---|---|---|
| `test: unknown scenario 'X'` | wrong id | check `wg list` |
| `NO_DEFAULT: ...` | preset lacks a standard default | supply the field via overrides or JSON `program` |
| `CUSTOM: ...` | user must choose a channel/reference or value | follow the message |
| `EXCLUDED: ...` | outside generator scope | do not substitute digital injection for hardware tests |
| `duration_s: expected number in [0.001, 7200]` | duration out of range | use 0.001–7200 s |
| `sample_rate_hz: expected integer in [1000, 1000000]` | rate out of range | use 1 k–1 MHz |
| `frame_samples: expected integer in [1, 65535]` | frame length out of range | use 1–65535 |
| `segments: expected total duration >= duration_s` | segments too short | make the sum equal `duration_s` |
| `segments: total duration exceeds duration_s` | segments too long | same |
| `mod: not supported with segments` | `--mod` used with segments | move modulation into JSON `program` |
| `program.components: expected at least one fundamental` | no fundamental component | add one |
| `program.modulators: expected at most 1 FREQ_* entry` | duplicate frequency profile | keep one |
| `program.components[0].order: expected integer 2..64` | invalid harmonic order | use an integer 2–64 |
| `stream mode: use C API` | `--stream` on CLI | use `wg_run_stream()` |
| `config: cannot open '...'` | bad JSON path | check path and permissions |
| `generate: failed to write '...'` | unwritable path or full disk | change path or free space |
| Hashes differ between runs | default start UTC is "now" | fix it with `--start` |
| Verifier reports `DYNAMIC`/`PARTIAL` | modulated or ramped content | expected; steady single-value checks do not apply |
| `selftest` fails to build on Windows | missing POSIX headers | build under WSL/Linux |

## 12. Scope and limits

- WaveForge produces mathematical waveforms. It cannot prove ADC, analog front-end, anti-aliasing, transducer or clock performance.
- A software verification PASS is not an IEC type test or a metrology certificate.
- All preset values are defaults, not the standard text. Obtain the standards from IEC or an authorised channel.
- `NO_DEFAULT` means "no built-in default", not "cannot generate".
