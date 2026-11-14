# GhostNet Monitor

Receive-only Android monitor for the [GhostNet](https://github.com/s2underground/GhostNet)
network: RTL-SDR in; JS8, RTTY and 2G ALE decoded and logged (ALE with
10-channel scanning); LSB and VHF/UHF FM voice played live (never recorded). There is no transmit path anywhere in
the design.

**Current state (v0.6):** JS8, RTTY, 2G ALE, LSB voice and FM voice are
code-complete and pass all software tests, including full-chain tests in
RTL-SDR IQ format and on arm64. Nothing has run on real hardware yet;
[TESTING.md](TESTING.md) is the procedure. VARA, Winlink, DMR and
GhostMesh are Coming Soon per the spec.

## Documents

| Document | What it is |
|---|---|
| [`TESTING.md`](TESTING.md) | **Start here to test with hardware.** Step-by-step, each step isolating one layer. |
| [`PHASE1_PLAN.md`](PHASE1_PLAN.md) | What's built, what's verified, and what's still open. |
| [`GhostNet_Monitor_Android_Technical_Spec_V1.0.md`](GhostNet_Monitor_Android_Technical_Spec_V1.0.md) | Full V1 product target. |
| [`PHASE0_FINDINGS.md`](PHASE0_FINDINGS.md) | The original feasibility pass. |
| [`THIRD_PARTY.md`](THIRD_PARTY.md) | Licences, provenance, the one local patch, and how dependencies are built. |

## Layout

| Path | Contents |
|---|---|
| `android/` | The app: foreground service, session database, UI, JNI bridge |
| `rx-core/` | Native receive core: RTL-SDR source, pipeline and modes, JS8 frame unpacking and message assembly, RTTY decoder, 2G ALE decoder and channel scanner, waterfall, recording, `ghostnet-rx` desktop tool, tests |
| `rf-frontend/` | DSP: IQ → SSB or narrowband FM audio, with tests |
| `third_party/` | js8core (MIT), rtl-sdr-blog (GPLv2, patched), libusb (LGPL-2.1), Eigen, CRCpp |

## Signal path

```
RTL-SDR V3 (Q-branch direct sampling, 240 kS/s u8 IQ, tuned dial + 25 kHz)
  -> libusb (Android: wrapped UsbManager fd) -> librtlsdr async read
  -> bounded queue (drops and counts on overload)
  -> rf-frontend: mix -> /5 -> /4 -> USB phasing demod -> slow AGC -> 12 kHz PCM
  -> js8core engine (receive only) -> frames
  -> rx-core: frame unpack -> multi-frame message assembly
  -> app: SQLite session log + live display
```

## Build and test without hardware

```bash
cmake -S rx-core -B rx-core/build -DCMAKE_BUILD_TYPE=Release
cmake --build rx-core/build -j
ctest --test-dir rx-core/build --output-on-failure   # ~1 min

cmake -S rf-frontend -B rf-frontend/build && cmake --build rf-frontend/build -j
ctest --test-dir rf-frontend/build --output-on-failure
```

The Android build is described in [`android/README.md`](android/README.md).
