# GhostNet Monitor

Receive-only Android monitor for the [GhostNet](https://github.com/s2underground/GhostNet)
HF digital network: USB SDR / radio input in, decoded traffic and schedule
out, no transmit path anywhere in the design.

## Where things are

| Document | What it is |
|---|---|
| [`GhostNet_Monitor_Android_Technical_Spec_V1.0.md`](GhostNet_Monitor_Android_Technical_Spec_V1.0.md) | The full V1 product target: modes, frequencies, schedule, UI, data model, acceptance tests. |
| [`PHASE1_PLAN.md`](PHASE1_PLAN.md) | **Start here for current work.** Re-sequences the spec's phases around a single narrow milestone (RTL-SDR → JS8, real hardware, one real decode) before any other input/mode is started, and tracks what's actually done. |
| [`PHASE0_FINDINGS.md`](PHASE0_FINDINGS.md) | What the original feasibility pass actually proved vs. assumed, before this plan existed. |
| [`THIRD_PARTY.md`](THIRD_PARTY.md) | License and provenance of everything under `third_party/`, including a correction to the spec's assumption about the JS8 decoder's license. |

## Layout

- `rf-frontend/` — first-party DSP (RTL-SDR-style real-sample-stream → demodulated audio). Portable C++20, host-buildable, host-tested against synthetic fixtures. No Android/USB dependency (yet).
- `phase0-poc/` — the Android harness: USB enumeration, JNI bridge into the JS8 decoder, foreground service, SQLite schema. Minimal by design; see `PHASE1_PLAN.md` for what it still needs.
- `third_party/` — vendored dependencies (`js8core`, `libusb`, `rtl-sdr-blog`, `Eigen`, `CRCpp`). See `THIRD_PARTY.md` before adding to or relying on any of these.

## Building what can be built without an Android SDK

```bash
cd rf-frontend
cmake -S . -B build && cmake --build build -j
ctest --test-dir build --output-on-failure
```

The Android app (`phase0-poc/`) needs an Android SDK/NDK and a connected
device to do anything beyond compile; see `phase0-poc/README.md`.
