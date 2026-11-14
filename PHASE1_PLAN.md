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

**Deferred past M1:** DigiRig Mobile/Lite, RTTY, 2G ALE, analog voice.
(The four-tab UI, schedule, export and settings were brought forward and
are done; see below.)

### WS1 — Repo hygiene and provenance

- [x] `.gitignore`; no build output committed.
- [x] `THIRD_PARTY.md` with actual licences, the one local patch, and how
      each dependency is built.
- [x] NDK pinned (`25.1.8937393`) in `gradle.properties` and
      `app/build.gradle.kts`.
- [ ] Submodules vs. vendored snapshot, with pinned upstream commits. Needs
      network access to the upstream hosts.

### WS2 — Licence posture (decided)

The vendored `js8core` is an independent MIT reimplementation, not the
GPLv3 JS8Call Android port the spec assumes; spec sections 4.1 and 9.5
need updating. The real copyleft surface is `rtl-sdr-blog` (GPLv2, now
patched and statically linked) plus `libusb` (LGPL-2.1, statically linked).

- [x] **Decided by the owner (2026-11-14):** the app is published as open
      source under GPL-2.0-or-later (`LICENSE`, README). This matches the
      licence of the bundled `rtl-sdr-blog` driver and satisfies libusb's
      LGPL relinking condition, since the full source is available.
- [ ] Before public release: make the GitHub repository public, and attach
      the APK to a release together with the matching source (a tagged
      commit), so every distributed APK has its exact source available.
- [x] Spec 4.1, 9.5, 12.1 and 12.3 updated to name js8core (MIT); the GPLv3
      port is recorded as evaluated in Phase 0 and not used.

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

### Done ahead of M1 (hardware-independent)

- [x] GhostNet V1.5 frequency catalog and UTC schedule engine
      (`GhostNetPlan.kt`, `ScheduleEngine.kt`). Covers the spec 11.3
      frequency/schedule checks: 26 exact frequencies, 12 Thursday + 11
      Saturday slots, Florida local labels through DST transitions, and the
      page 17 VARA erratum. 11 JVM unit tests. The main screen shows the
      current/next window; non-JS8 modes are labelled Coming Soon.
- [x] CI (`.github/workflows/ci.yml`): host native tests (also with ARM
      char semantics), Android unit tests, APK build, a check that APK
      permissions stay receive-only, and the APK uploaded as an artifact.

### Application completed ahead of hardware test (v0.3-m1)

- [x] Jetpack Compose four-tab app per spec 7.1: Receive (presets, status
      lines, live waterfall with decode markers, messages/raw frames),
      Schedule (all windows, region filters, tap-to-tune JS8, Coming Soon
      for other modes, full frequency catalog), Export (session browser,
      JSON/CSV/TXT, IQ recordings as zip, diagnostics), Settings (receiver,
      recording, diagnostics/self-test, retention, about/licences).
- [x] Waterfall: native 2048-point FFT of the demodulated audio, 10 Hz
      bins 0-3 kHz, ~12 rows/s; tested to show a synthetic JS8 signal at
      its offset (30 dB clean, 12 dB weak above the noise median).
- [x] On-tablet raw IQ recording (.cu8 + .meta, size-capped, dropped spans
      kept as neutral samples); tested byte-exact and replays to the same
      decoded message.
- [x] Diagnostics: persistent rotating event log, librtlsdr/libusb stderr
      captured (Android discards it otherwise), periodic receiver stats,
      Java crash capture, Android exit reasons (incl. native crash/ANR),
      single-file export with device/USB/settings/sessions/logcat.
- [x] Robolectric UI and data tests (16 JVM tests in total). These caught
      a real bug: the retention query compared a number to a text-bound
      parameter, which would have deleted every finished session.
- Deferred, needs a decision or hardware first: auto clock-drift
  correction, release signing and licence screen (WS2), DigiRig / RTTY /
  ALE / analog.

### More receive modes (v0.4, ahead of hardware test)

- [x] Analog voice, live playback only (never stored, per spec 2.3/8.5):
      LSB for the 7.190 MHz fallback (existing SSB path) and VHF/UHF
      narrowband FM (new demodulator: 37.6 dB adjacent-channel rejection,
      de-emphasis, noise squelch). TEST presets on NOAA Weather Radio.
- [x] RTTY 45.45 baud: own decoder (matched filters, start-bit sync,
      US-TTY Baudot, unshift-on-space), automatic tone-pair search,
      reverse and 170/425/850 Hz shift options, framing errors shown as
      `~` and never guessed, quality gate so noise prints nothing. Tested
      standalone and through the full IQ chain.
- [x] Receive modes in the native receiver; the JS8 engine only runs in JS8
      mode; voice mode refuses IQ recording even if configured.
- [x] **2G ALE (v0.5).** Own implementation (`rx-core/src/ale.cpp`): 8-FSK
      on the USB audio, 8 parallel symbol-timing phases, triple-redundancy
      majority vote, Golay (24,12) correcting 3 errors per half, strict
      acquisition (≤1 correction per half + a plausible address word) and
      continuation sync. Protocol layer: TO/TIS/TWS/THRU/FROM addresses with
      DATA/REP extension, soundings, calls, AMD text; other commands are
      logged raw, never interpreted. Scanner: 10 GhostNet channels, 1 s
      dwell (setting), stops on any word, resumes 4 s after the last; data
      in flight in USB buffers when retuning is blanked, never attributed to
      the new channel. Tests: every word decodes in AWGN down to −6 dB SNR
      (3 kHz), threshold ~−7 dB; ±40 Hz mistuning; 0 false words in 10 min of
      noise, carrier and sweep; full IQ-chain scan test with a simulated
      dongle that retunes. Format cross-checked against LinuxALE (GPLv2,
      reference only); PC-ALE rejected (see THIRD_PARTY.md).
- [x] **Odds-improvement pass (v0.6), no hardware needed:**
      - Spectrum orientation: the driver disables spectrum inversion in
        direct sampling and tunes the DDC to the exact frequency, so Invert
        off is expected to be correct on all HF channels. Backstop: an
        automatic check (JS8) tries the other orientation after 10 min with
        no decodes, reverts after another 10 if that fails, and the first
        decode confirms and saves the setting for all modes. Tested both ways.
      - Retunes: verified against the driver's reported frequency, one
        retry, each channel's actual tuning logged once, a channel failing
        3 times in a row skipped for 5 min (tested with a simulated
        refusing channel), option to leave out 28.1085 MHz.
      - WAV input (`Receiver::push_audio`, `ghostnet-rx --wav`): real
        off-air audio recordings run through the same decoders. Tested with
        48 kHz and 44.1 kHz files (ALE, RTTY).
      - arm64: the full end-to-end suite passes under QEMU at 1.5x speed,
        including JS8 and the self-test; the earlier failures were the
        emulator not keeping up at 10x.
- [ ] **Real recordings (owner, no hardware):** sample WAVs of ALE, JS8 and
      45.45 baud RTTY from the web, run through `--wav`. This is the one
      remaining way to check the ALE format details before a hardware test.
- [ ] **ALE on air (owner):** no real ALE signal has been decoded yet. The
      B-codeword check-bit inversion and the AMD/command split come from
      LinuxALE and my reading of the standard, not from a recording.
      Retuning while streaming, including the switch out of direct sampling
      for 28.1085 MHz, is unverified on hardware.

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
