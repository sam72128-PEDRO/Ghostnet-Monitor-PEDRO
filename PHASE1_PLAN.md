# GhostNet Monitor — Phase 1 build plan

Date: 2026-11-14
Status: Draft, supersedes the phase ordering in the technical spec section 11.1
Relation to other documents: `PHASE0_FINDINGS.md` records what Phase 0 actually
proved (decoder core builds and passes synthetic vectors on host; nothing RF-
facing has run). `GhostNet_Monitor_Android_Technical_Spec_V1.0.md` describes the
full V1 product target. This document re-sequences the work between those two:
it does not change the target, it changes the order and the size of the first
real milestone.

## Why re-sequence

The spec's Phase B through F treat three input adapters (RTL-SDR, DigiRig
Mobile, DigiRig Lite) and four decode modes (JS8, analog, RTTY, 2G ALE) as
parallel work on one timeline. They aren't independent: every one of them
depends on a receive chain that has never been exercised end-to-end, on real
hardware, with a real signal. Right now the only two things with evidence
behind them are USB device enumeration (phase0-poc, host-tested logically but
not on a device) and the JS8 decoder core compiling and passing synthetic
vectors on a Mac host. Nothing has captured an RF sample, demodulated
anything, or run on an Android device.

So Milestone 1 below is deliberately narrow: one input adapter, one mode, one
real decode on real hardware. Everything else in the spec is still the target
— it's just explicitly out of M1's scope, tracked separately, and gated behind
M1 passing.

## Milestone 1: RTL-SDR → JS8 vertical slice

**Goal:** RTL-SDR Blog V3 → USB permission → direct-sampling IQ capture →
native DSP → mono 12 kHz PCM → js8core decode → UI → session log, proven on
the SM-T733 with at least one real decoded JS8 frame (clean signal) and one
low-SNR capture. This is exactly Phase 0's own recommended next steps
(PHASE0_FINDINGS.md, "Recommended implementation sequence," items 2–6),
restated as the gate for everything else.

**Explicitly deferred past M1** (stay on the backlog, don't start): DigiRig
Mobile, DigiRig Lite, RTTY, 2G ALE, analog voice, Export tab, Schedule
engine/recurrence, Settings screen, full four-tab Compose navigation. Building
nav chrome or a second input adapter before the one hard RF/DSP path is proven
is work that would need to be redone or reconsidered once real hardware
surfaces problems.

### WS1 — Repo hygiene and provenance (do first, cheap, no ambiguity)

- [x] `.gitignore` added (build/, .cxx/, .gradle/, local.properties, *.apk, keystores).
- [x] Debug APK removed from the tree; builds are never committed.
- [x] `THIRD_PARTY.md` added recording actual license per vendored dependency.
- [ ] Decide submodule vs. vendored-snapshot for `js8core`/`libusb`/`rtl-sdr-blog`.
      Blocked on this environment's network policy (no outbound access to
      their upstream hosts right now) — do this step from an environment or
      session that can reach GitHub/upstream, then record the pinned commit
      in `THIRD_PARTY.md`.
- [x] NDK version pin. `gradle.properties` already pinned `25.1.8937393`
      project-wide and `CMakeLists.txt`/`build.gradle.kts` were already
      consistent with it — the mismatch Phase 0 hit was against the
      GPLv3 Android-port candidate's own README (`26.1.10909125`), which is
      no longer part of this project (see WS2). Added an explicit
      `ndkVersion = "25.1.8937393"` in `app/build.gradle.kts`'s `android {}`
      block too, so a module-level override can't silently drift from
      `gradle.properties` later.

### WS2 — License posture decision (do before WS4 links more code)

The real finding here, corrected from my first read of this: the vendored
`third_party/js8core` is an **independent MIT reimplementation** of JS8, not
the GPLv3 JS8Call Android port that `PHASE0_FINDINGS.md` evaluated in Phase 0.
Its README states the FIR coefficients and lookup tables are generated, not
copied from the GPL desktop source. That means the decoder itself carries no
GPL obligation. The technical spec section 4.1/9.5 still describes integrating
"JS8Call-improved/Android-port" (GPLv3) — that's now stale; the project has
already moved past it to the cleaner MIT core, and the spec should say so.

The real copyleft surface is narrower than the spec assumes, but it's not
zero: `rtl-sdr-blog` (the actual RTL2832U/R820T2 driver) is **GPLv2**.
`libusb` is LGPL-2.1, which is fine to dynamically link without triggering
anything. Before WS3 links `rtl-sdr-blog` into the native module:

- [ ] Decide: ship the whole app under GPLv2-compatible terms (simplest — this
      is what essentially every RTL-SDR Android app does), or isolate
      `rtl-sdr-blog` behind a process/IPC boundary to keep the rest of the app
      under different terms. For a receive-only hobbyist/prepper tool, taking
      the GPLv2 terms for the whole app is almost certainly the right call —
      flagging the decision point rather than making it silently.
- [ ] Update the technical spec's section 4.1 and 9.5 to describe the actual
      MIT `js8core` dependency instead of the GPLv3 Android-port candidate.

### WS3 — RTL-SDR native capture + DSP (the actual unproven hard part)

This is the real risk in M1 and the reason it's scoped alone. Current state:
`RtlSdrCapture.kt` claims a bulk-IN USB interface and does raw
`bulkTransfer` reads — it has no RTL2832U register programming, no sample
rate configuration, no tuner/Q-branch mode selection, no gain or PPM control.
It reads raw USB bytes, not calibrated IQ.

- [x] **Demodulation math**, built and host-tested in `rf-frontend/`
      (portable C++20, no Android/USB dependency yet). Complex-mix down by
      the tuned offset, band-limit and decimate, then separate the wanted
      sideband from its image with a phasing-method Hilbert combine. Two
      synthetic-fixture tests pass: image rejection (~83 dB with 129/127-tap
      filters, floor set at 20 dB) and passband skirt attenuation (~59 dB).
      This caught a real bug along the way — the first version applied the
      Hilbert transform before the mix instead of after, which measured 0 dB
      image rejection; worth noting as a concrete example of why this gets
      validated against a fixture before real RF, not just eyeballed. See
      `rf-frontend/README.md` for exactly what is and isn't proven by this —
      it says nothing about real RTL2832U sample behavior, achievable USB
      throughput on the SM-T733, or front-end nonlinearity.
- [ ] Native module wrapping `rtl-sdr-blog` (librtlsdr) against the Android
      USB file descriptor, per the `libusb_wrap_sys_device` +
      `LIBUSB_OPTION_NO_DEVICE_DISCOVERY` pattern Phase 0 already identified
      as correct for Android. Still not started — this is USB/device-specific
      and the highest-uncertainty remaining piece of WS3.
- [ ] RTL2832U/R820T2 control: direct-sampling Q-branch mode, sample rate,
      gain, PPM correction, for the three JS8 presets (3.575 / 7.107 / 14.107
      MHz).
- [ ] Wire `rf-frontend`'s `SsbDemodulator` to a real capture stream and to
      `js8core`'s mono 12 kHz PCM input (the module already targets that
      rate by default; what's missing is a real producer of int16 samples
      at a rate that's an integer multiple of it — see the README's note on
      the decimation-ratio assumption).
- [ ] Bounded ring buffer between USB capture and DSP/decode, drop-and-count on
      overload rather than unbounded growth (already sketched in
      PHASE0_FINDINGS.md's recommended chain).

### WS4 — JS8 decode wiring (partially exists, needs to become a real pipeline)

`phase0_jni.cpp` currently does one fixed-size, one-shot decode call with
hardcoded params (`nfa`/`nfb`/`nfqso`/`nsubmodes`) — it's a proof that the JNI
boundary works, not a streaming decoder.

- [ ] Turn the one-shot JNI call into a continuous decode loop consuming the
      WS3 ring buffer.
- [ ] Replace `GhostNetDatabase`'s single raw `traffic` table with the
      session/message schema from spec section 8 (sessions and messages as
      separate tables with a foreign key, not one flat table) — Room is the
      spec's stated choice; raw `SQLiteOpenHelper` is fine to keep for M1 if it
      already matches the session/message split, no need to add Room just for
      ceremony.
- [ ] Implement the session state machine from spec 2.4
      (IDLE→CONNECTING→READY→RECEIVING→STOPPING→IDLE, ERROR branch) —
      `MainActivity` currently has a single `monitoring: Boolean`, which can't
      represent "hardware connected but no signal" vs. "signal but no decode."

### WS5 — Minimal UI for the vertical slice only

Not the spec's four-tab nav yet. Just enough to drive and observe M1:

- [ ] Three JS8 presets, Start/Stop, and the three independent status
      indicators from spec 7.2 (Device / Signal / Decoder) — this distinction
      matters for diagnosing M1 failures (is it USB, is it DSP, or is it the
      decoder) and costs little to add now.
- [ ] Live decoded message list with UTC timestamp and raw text.
- [ ] Full Compose four-tab nav (Receive/Schedule/Export/Settings) is Phase A
      material and can wait — chrome before the engine works is low value.

### WS6 — Hardware validation on the SM-T733

Nothing here can be faked or simulated; this is the actual gate.

- [ ] USB permission grant/deny, unplug/replug, powered-hub charging,
      screen-off foreground-service behavior.
- [ ] At least one clean-signal JS8 decode and one low-SNR decode, either from
      a live GhostNet schedule window or a known-good recording fed through
      the real RF chain (not injected as PCM — through the antenna/SDR).
- [ ] Short soak (a few hours, not the spec's full four-hour target yet) to
      surface obvious thermal/battery/stability problems early.

### M1 acceptance

Matches the spec's own release-availability gate (section 1.4), scoped to one
input and one mode: real RTL-SDR V3 → direct-sampling IQ → native DSP →
js8core → UI → session log, tested on the SM-T733, with at least one real
decoded JS8 frame. Until this passes, DigiRig Mobile/Lite, RTTY, 2G ALE, and
analog voice stay on the backlog — each becomes its own milestone with the
same discipline (real hardware, real signal, before it's called available),
not a parallel phase on the same timeline.
