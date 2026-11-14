# GhostNet Monitor — Phase 1 build plan

Status as of 2026-11-14: **Milestone 1 is code-complete and passes every
software test. Real-hardware validation (WS6) is the only open gate.** See
[TESTING.md](TESTING.md) for the hardware procedure.

This document re-sequences the technical spec's phases (section 11.1). It
doesn't change the V1 target; it changes the order, and the size of the
first real milestone.

## Why re-sequence

The spec's phases B–F treat three inputs (RTL-SDR, DigiRig Mobile, DigiRig
Lite) and four modes (JS8, analog, RTTY, 2G ALE) as parallel work. They
all depend on a receive chain that had never been exercised end to end.
Milestone 1 is therefore one input, one mode, one real decode on real
hardware. Everything else stays on the backlog until that passes.

## Milestone 1: RTL-SDR → JS8 vertical slice

**Goal:** RTL-SDR Blog V3 → USB permission → direct-sampling IQ → native
DSP → 12 kHz PCM → js8core → frames → messages → UI → session log, on the
SM-T733, with at least one real decoded JS8 frame.

**Deferred past M1:** DigiRig Mobile/Lite, RTTY, 2G ALE, analog voice,
Schedule tab, Settings tab, full Compose four-tab navigation.

### WS1 — Repo hygiene and provenance

- [x] `.gitignore`; no build output committed.
- [x] `THIRD_PARTY.md` with actual licences, the one local patch, and how
      each dependency is built.
- [x] NDK pinned (`25.1.8937393`) in `gradle.properties` and
      `app/build.gradle.kts`.
- [ ] Submodules vs. vendored snapshot, with pinned upstream commits. Needs
      network access to the upstream hosts.

### WS2 — Licence posture (decision needed from the owner)

The vendored `js8core` is an independent MIT reimplementation, not the
GPLv3 JS8Call Android port the spec assumes; spec sections 4.1 and 9.5
need updating. The real copyleft surface is `rtl-sdr-blog` (GPLv2, now
patched and statically linked) plus `libusb` (LGPL-2.1, statically linked).

- [ ] **Decide:** publish the app's source under GPLv2 (recommended; it
      also satisfies the LGPL relinking condition), or restructure linking.
      This must be settled before any APK is distributed beyond personal
      testing.
- [ ] Update spec 4.1 / 9.5 to name js8core.

### WS3 — RTL-SDR capture and DSP

- [x] `rf-frontend/`: u8 IQ → mix → two-stage decimation → phasing-method
      SSB → slow AGC. Tests: 63 dB sideband rejection with u8 quantisation
      and noise, out-of-channel and DC rejection, invert switch, ~100x real
      time on host.
- [x] Correction recorded: the RTL2832U delivers complex IQ in direct-
      sampling mode (its DDC runs after the ADC); earlier docs said real
      samples.
- [x] `rtl-sdr-blog` patched with `rtlsdr_open_sys_dev()` for Android file
      descriptors; libusb and librtlsdr built via CMake.
- [x] `RtlSdrSource`: 240 kS/s, tuned 25 kHz above the dial so the DC spike
      is out of band; driver auto-selects Q-branch direct sampling below
      24 MHz; RTL AGC switch; bias-tee forced off; device-loss reporting.
- [x] Bounded USB→DSP queue that drops and counts on overload. Verified
      under CPU starvation (QEMU): it degrades without crashing.
- [ ] **Hardware:** whether Q-branch delivers an inverted spectrum. Exposed
      as the *Invert spectrum* switch; TESTING.md step 4.

### WS4 — JS8 decode, frames, messages, storage

- [x] Receive-only wrapper around the js8core engine (no audio output, rig
      or network dependency; transmit methods never called). Sample-count
      timestamps anchored to earliest USB arrival for UTC slot alignment.
- [x] Frame unpacking (heartbeat, CQ, compound, directed, data, fast data)
      and multi-frame message assembly by submode and offset. Duplicate,
      gap, missing-start, timeout and interruption handling. 40+ checks,
      with and without sender grid.
- [x] End-to-end synthetic test: `@GHOSTNET` message → RTL-format IQ with
      noise, DC offset and an off-channel carrier → full pipeline → correct
      text in the correct UTC slots, including weak signal and inverted
      capture. Also passes with ARM `char` semantics (`-funsigned-char`)
      and as a real aarch64 binary under QEMU.
- [x] SQLite: sessions / frames (raw always kept) / messages, foreign keys,
      crash recovery marks orphaned sessions Interrupted, zero-decode
      sessions kept.

### WS5 — Minimal Android UI and service

- [x] `android/` (was `phase0-poc/`, now `org.ghostnet.monitor`). Builds;
      APK verified to request only FOREGROUND_SERVICE(+CONNECTED_DEVICE),
      POST_NOTIFICATIONS and WAKE_LOCK. JNI signatures checked against the
      compiled Kotlin.
- [x] Foreground service owns the session with a partial wake lock for
      screen-off reception; USB detach and source errors end the session as
      Interrupted.
- [x] Device / Signal / Decoder / Clock status lines (text, not colour
      alone), GhostNet presets plus clearly labelled TEST presets on the JS8
      calling frequencies, invert / RTL AGC / PPM, messages and raw-frames
      views, GhostNet-group display filter, JSON session export, on-device
      self-test.
- [x] `ghostnet-rx` desktop tool: live, record (`.cu8` + `.meta`) and
      replay, so field captures can be debugged offline.

### WS6 — Hardware validation on the SM-T733 (owner)

- [ ] Self-test PASS on the tablet, with its real-time factor.
- [ ] USB permission, unplug/replug, OTG vs. powered hub.
- [ ] Real JS8 decode on a TEST calling frequency; record which invert
      setting works.
- [ ] Real GhostNet window decode.
- [ ] Screen-off and one-hour run: dropped samples, battery, heat.

### M1 acceptance

All WS6 boxes ticked. Then DigiRig, RTTY, ALE and analog each become their
own milestone with the same rule: real hardware and a real signal before
anything is called available.
