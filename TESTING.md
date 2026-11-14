# Milestone 1 hardware test guide

This checks the one thing nothing so far has proven: **a real RTL-SDR on
real HF, decoded on the SM-T733.** Run the steps in order. Each one
isolates a layer, so a failure points at software, USB, RF or the clock
rather than "it didn't work."

The app is receive-only. It has no transmit, reply or keying function,
and requests no internet, microphone or location permission.

## What you need

- RTL-SDR Blog V3
- An HF antenna on the V3's SMA input. A random wire or dipole for 40 m is
  enough. Direct sampling is not a substitute for an antenna.
- USB-C OTG adapter, or a powered USB-C hub (try both; see step 6)
- Galaxy Tab S7 FE (SM-T733)
- Optional: a Mac or Linux computer for step 2

## Step 0: install the app

Either sideload the prebuilt debug APK (`app-debug.apk`) or build it:

```bash
cd android
./gradlew :app:assembleDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

Building needs JDK 17, Android SDK platform 35, NDK `25.1.8937393` and
CMake `3.22.1`. Point `local.properties` at your SDK (`sdk.dir=...`).

## Step 1: self-test on the tablet (no radio)

Open GhostNet Monitor and tap **Self-test**. It runs a built-in synthetic
JS8 transmission, in the exact format the RTL-SDR delivers, through the
full demodulator and decoder on the tablet. It takes about 20 seconds.

- **PASS** with `N0CALL: @GHOSTNET SELF TEST` means the native code works on
  this tablet. Note the "x real time" figure: it must be well above 1.
- **FAIL** means a software problem on this device. Report the whole dialog
  text and stop here; RF testing can't tell us anything yet.

## Step 2 (optional but recommended): test the SDR on a computer

This separates "the dongle and antenna work" from "Android USB works."

```bash
cd rx-core
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/ghostnet-rx --list
./build/ghostnet-rx --preset test40 --record test40.cu8
```

Leave it running 10–15 minutes in the evening (40 m) or daytime
(`--preset test20`). Decoded frames print as they arrive; `>>>` lines are
reassembled messages. Every 5 seconds a `[status]` line shows the input
level.

On Linux, if the device won't open, a DVB-T kernel driver may hold it, or
you may need a udev rule. Running once with `sudo` tells you which. The
macOS build is untested (it uses libusb's macOS backend); if it fails to
compile or open the device, skip to step 3.

**If nothing decodes, keep the recording.** `test40.cu8` plus
`test40.cu8.meta` is raw IQ I can replay and debug offline. Ten minutes is
about 290 MB, so trim it with `--seconds 300` if that's too big to send.

## Step 3: connect the receiver to the tablet

Plug the V3 into the tablet. Android should ask whether to open GhostNet
Monitor for this device: say yes, and tick "always" if offered. The
**Device** line should read `OK ... ready`.

If it shows `CHECK ... tap Find receiver`, tap **Find receiver** and grant
access. If the device isn't seen at all, try the other USB route (direct
OTG vs. powered hub).

## Step 4: receive on a busy JS8 frequency first

Select **TEST · JS8 calling 7.078 MHz** (evening/night) or **TEST · JS8
calling 14.078 MHz** (daytime). These aren't GhostNet frequencies; they
carry JS8 traffic most hours, so they prove reception without waiting for a
scheduled net. Tap **START RECEIVING**.

Watch the four status lines:

| Line | What good looks like | If not |
|---|---|---|
| Device | `OK Receiving · RTL-SDR ... direct sampling 2` | `direct sampling 0` below 24 MHz means Q-branch mode didn't engage; report it |
| Signal | `OK Samples flowing · IQ about -20 to -45 dBFS` | `No samples`: USB or power problem. `Very weak`: antenna. `Overloaded`: turn **RTL AGC** off, or use a shorter antenna |
| Decoder | Frames appear within a few minutes | See the troubleshooting order below |
| Clock | `Timing good` | `Signals arrive ±x s off the slot`: set the tablet to automatic network time |

Give it 15 minutes before concluding anything; JS8 activity comes and goes.

### Troubleshooting order when the Signal line is OK but nothing decodes

1. **Toggle Invert spectrum** (stop, flip it, start again). This is the
   most likely cause and the one I couldn't verify without hardware:
   whether Q-branch direct sampling flips the spectrum. A synthetic test
   shows the decoder gets nothing at all when the orientation is wrong.
2. **RTL AGC.** Try the other setting.
3. **PPM.** The V3 has a 1 ppm TCXO, so this should rarely matter for JS8.
   Leave it at 0 unless you have a known correction.
4. **Clock.** A tablet more than about 2 s off UTC decodes nothing. Turn on
   automatic date and time.

Whatever happens, tap **Export last session** and send me the JSON. It
contains every decoded frame raw, plus input levels, dropped-sample count
and timing statistics.

## Step 5: GhostNet presets

Once step 4 decodes, switch to the GhostNet presets during scheduled
windows (UTC is authoritative; Florida times shown for EST / EDT):

| Window | UTC | Florida | Preset |
|---|---|---|---|
| North America regional net (JS8) | Fri 01:00–01:30 | Thu 8:00 PM EST / 9:00 PM EDT | 40 m · 7.107 |
| NA–Australia bridge | Sat 12:00–13:00 | Sat 7:00 AM EST / 8:00 AM EDT | 20 m · 14.107 |
| NA–Australia bridge | Sat 13:30–14:00 | Sat 8:30 AM EST / 9:30 AM EDT | 80 m · 3.575 |
| NA–Europe bridge | Sat 18:00–19:00 | Sat 1:00 PM EST / 2:00 PM EDT | 20 m · 14.107 |

Turn on **GhostNet groups only** to hide non-GhostNet traffic. This only
affects the display; everything decoded is still stored.

## Step 6: endurance and failure handling

- **Screen off.** Start receiving, lock the tablet for 15 minutes, unlock.
  The frame count should have kept rising, and the notification should
  stay up the whole time.
- **Unplug while receiving.** The session should end as `Interrupted` with
  `device_removed`, and the app should show the error without crashing.
- **Power.** Note whether the tablet charges or drains through a powered
  hub during a long session. Note any heat.
- **One-hour run.** Note battery percentage at start and end, the dropped-
  sample count (Signal line), and whether the Decoder line kept updating.

## What to send back

- The self-test dialog text (step 1)
- Exported session JSON from steps 4 and 5
- A screenshot of the four status lines while receiving
- Which **Invert spectrum** setting decoded (this settles the open question)
- If nothing decoded: a `ghostnet-rx --record` capture, if you could make one
