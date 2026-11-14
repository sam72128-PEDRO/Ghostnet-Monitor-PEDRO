# GhostNet Android Phase 0 findings

Date: 2026-11-08  
Scope: Phase 0A decoder feasibility, 0B USB-SDR feasibility, and 0C end-to-end feasibility. Full UI and future communications modules are intentionally out of scope.

## Executive result

The JS8Android candidate is technically usable as an independent receive-side native engine after a receive-only extraction. Its core decoder builds on the host and passes the repository's synthetic 135-case round-trip test. Its native API accepts mono 16-bit PCM with a 12 kHz engine path and emits decoded frame metadata. The candidate also includes Android JNI and multipart assembly tests.

Phase 0 is not complete for the target hardware. No RTL-SDR Blog V3 was connected to this Mac or to the Galaxy Tab S7 FE, no physical SM-T733 was available to inspect, and no IQ-to-audio path has been exercised. Therefore USB permission, Q-branch capture, hub charging, screen-off operation, battery, thermal, clock drift, and RF-to-JS8 decoding remain unverified.

## Governing inputs

- The supplied V1.1 addendum is the controlling Android specification.
- The supplied `GhostNet_Version_1.5.pdf` SHA-256 is `35d16261a3bff6790710bdaa5d864ad5d90e03d018957bcce158968a822ae00f`, identical to the official `s2underground/GhostNet` repository copy.
- V1.1 fixes the three presets at 3.575, 7.107, and 14.107 MHz, defaults groups to `@GHOSTNET` and `@GSTFLASH`, and requires receive-only offline operation.

## 0A decoder feasibility

### Source inspection

Pinned candidate repository commit: `d0d1ccabb057f6e0e268e2f255e97cc52f821adf`.

The repository has a platform-agnostic `core` library, Android adapters, JNI bindings, a Kotlin wrapper, an Android foreground decoding service, and packaged reference WAV tests. The JNI wrapper exposes audio submission and decoded callbacks. The Kotlin wrapper documents mono 16-bit PCM and provides `submitAudio` and `submitAudioRaw`. The Android reference test assets are 12 kHz mono 16-bit WAVs. Multipart assembly is implemented in `DecodeViewModel` and covered by seven unit-test cases, including directed group headers and multi-frame `MSG` reconstruction.

The candidate is not receive-only as checked out. `JS8Engine.kt` and JNI expose `transmitMessage`, `transmitFrame`, tune, PTT/transmit state, and transmit audio methods. The app manifest requests `RECORD_AUDIO`, `INTERNET`, network state, location, Bluetooth, and foreground microphone permissions. Those paths and permissions must be removed or isolated before GhostNet production use.

### Actual tests run

Host CMake build with AppleClang C++20 succeeded for `js8core`, `js8core-decoder-roundtrip-test`, and `js8core-wav-decode-test`.

- Synthetic decoder round-trip: **PASS, 135/135 cases** across JS8 modes A/B/C/E/I, three carrier offsets, three noise levels, and three message payloads.
- Reference WAV decode: **PASS**. `A_1_4.wav` produced 4 decodes, `A_2_3.wav` 3, `A_2_9.wav` 8, `E_1_1.wav` 1, and `E_2_1.wav` 1. Counts matched the vector names and the native test's expected counts.
- Host timing: two A-mode WAVs decoded in 0.51 s, 0.30 s user CPU, and 79,495,168 bytes maximum RSS.
- Synthetic round-trip timing: 6.11 s wall, 5.45 s user CPU, and 176,455,680 bytes maximum RSS for the full 135-case process. These are Mac host figures.

The Android Gradle project was exercised twice. The default Java selection failed because the host selected Java `25.0.2`, which this Kotlin/Gradle combination cannot parse. A Java 17 retry reached CMake configuration, automatically installed the candidate's requested NDK `25.1.8937393` and CMake `3.22.1`, then failed because the checked-out project requires a prebuilt Hamlib directory at `android/libs/hamlib/arm64-v8a`. No APK was produced. The separately installed SDK also has NDK `27.2.12479018`; the candidate's README names `26.1.10909125`, so the final build must pin one tested NDK and document it.

This workspace now contains a smaller receive-only harness under `phase0-poc/`. It builds successfully for `arm64-v8a` with Java 17, NDK `25.1.8937393`, and CMake `3.22.1`. The APK has not been installed or executed because no Android device is attached.

The harness APK build completed successfully. The current USB-probe artifact SHA-256 is `23b73461d4e662fd92749fccfc6d1c0c2bce5e0b22c6b28d4d7341d3d27cc243`. `aapt` confirms package `org.ghostnet.phase0`, min SDK 26, target SDK 35, and the `arm64-v8a` native target. ADB reports no attached devices.

The app has since been expanded into the first GhostNet monitor shell with the three V1.1 frequency presets, default GhostNet groups, START/STOP controls, a foreground notification service, USB permission scanning, and a private SQLite traffic schema. The rebuilt debug APK passes Gradle packaging. Direct RTL-SDR bulk capture and IQ demodulation are still the remaining hardware-facing implementation.

### Decoder conclusion

**Usable independently for reception, conditionally.** The core and JNI boundaries do not require JS8Call to be installed. Complete on-air JS8 message reconstruction is represented and unit-tested in the Android code, but the test evidence available here is synthetic and unit-level. A physical Android instrumentation run and an independently verified multipart recording are still required before acceptance.

## 0B RTL-SDR USB feasibility

Android USB host mode provides the needed supported permission flow through `UsbManager`, `UsbDeviceConnection`, and a native file descriptor. libusb documents `libusb_wrap_sys_device` with `LIBUSB_OPTION_NO_DEVICE_DISCOVERY` for Android, which is the appropriate pattern after Android grants access. The RTL-SDR Blog V3 documentation confirms Q-branch direct sampling for approximately 0.5-28.8 MHz, covering all three GhostNet presets.

The candidate repository does **not** contain an RTL2832U/R820T2 USB receiver implementation or a completed `librtlsdr` Android integration. Its USB code is primarily serial rig control and `AudioRecord`. It therefore cannot satisfy V1.1's direct-SDR requirement without a new native RTL-SDR module.

No USB device was attached in this environment. USB permission, VID/PID recognition, bulk transfer stability, Q-branch configuration, PPM/gain control, disconnect/reconnect, powered-hub charging, and tablet host-mode behavior are **not tested**.

## 0C end-to-end feasibility

Not passed. There is no tested chain from RTL-SDR IQ to demodulated 12 kHz PCM to the JS8 engine. The practical chain should be:

`UsbManager permission -> UsbDeviceConnection FD -> libusb/r820t2 control -> RTL2832U Q-branch IQ -> native narrowband DSP/demodulator -> bounded mono 12 kHz PCM ring -> JS8 core -> frame reassembly -> strict GhostNet destination parser/filter -> local persistence.`

Use one tuned RF band at a time. Keep USB acquisition, DSP, and decoding off the main thread. A bounded ring should drop and count samples on overload rather than grow without limit.

## Compatibility, licensing, and blockers

- Target ABI is `arm64-v8a`. The candidate currently configures both arm64 and armeabi-v7a. GhostNet can initially ship arm64 only.
- JS8Call/JS8Android code is GPLv3. Any distributed derivative containing the engine must preserve GPLv3 obligations and corresponding source availability. The candidate also bundles other dependencies whose licenses need a release manifest.
- The candidate app has transmit and network functionality. Production extraction must delete TX JNI/Kotlin APIs, modulation output, PTT/CAT, autoreply/relay/heartbeat behavior, network reporting, and unnecessary permissions. Compile-time removal is preferable to UI hiding.
- V1.1 requires Compose, Room/SQLite, DataStore, and a foreground service. The candidate includes Android service/database code, but it is not yet a GhostNet receive-only product.
- `RECORD_AUDIO` is unnecessary for SDR reception and must be absent from the final manifest. USB host permission is user-mediated and does not require root.
- Physical SM-T733 tests are required for Android version/capability discovery, sustained CPU/RAM, screen-off service behavior, USB power negotiation, thermal throttling, battery drain, clock behavior, and 24-hour stability.

## Recommended implementation sequence

1. Create a small `js8core-rx` Android library from the candidate core and JNI receive API. Remove transmit symbols and microphone capture. Keep the 12 kHz PCM input contract and multipart callback tests.
2. Add an NDK-built RTL-SDR module using Android USB host permission, `UsbDeviceConnection.getFileDescriptor`, and libusb wrapped around that descriptor. Add RTL2832U control plus R820T2/Q-branch selection, gain, PPM, bounded IQ buffering, counters, and reconnect state.
3. Implement a native direct-sampling DSP path that produces the decoder's required mono 12 kHz PCM. Validate with a generated IQ fixture before RF testing.
4. Build a minimal test APK with only device enumeration, permission, tune, sample counters, audio injection, and decoder output. Do not add the full GhostNet UI.
5. On the physical SM-T733, test a powered OTG hub and direct adapter separately. Record Android build, USB descriptors, permission result, IQ loss rate, decoder latency, CPU, RSS, temperature, battery, and screen-off behavior.
6. Run 0C with a known JS8 recording, then a controlled RF signal at each of 3.575/7.107/14.107 MHz, then a real GhostNet schedule window. Only after those pass should Phase 1 UI begin.

## Phase 0 acceptance status

| Requirement | Status |
|---|---|
| Independent native decoder compiles | Pass on host |
| Complete JS8 decode path proven | Pass for synthetic/reference host vectors |
| Multipart reconstruction | Unit-test evidence present, Android instrumentation pending |
| Android ARM64 harness build | Pass, debug APK produced |
| RTL-SDR USB permission and capture | Probe built, physical test pending |
| HF Q-branch | Not tested |
| IQ-to-12 kHz PCM | Not implemented/tested |
| RF-to-JS8 end to end | Not passed |
| Physical SM-T733 performance | Not tested |
| Receive-only production extraction | Not implemented |

