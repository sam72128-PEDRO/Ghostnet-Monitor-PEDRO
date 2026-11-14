# Hardware test guide

This checks the one thing nothing so far has proven: **a real RTL-SDR on
real HF, decoded on the SM-T733.** Run the steps in order. Each one
isolates a layer, so a failure points at software, USB, RF or the clock
rather than "it didn't work."

The app is receive-only. It has no transmit, reply or keying function,
and requests no internet, microphone or location permission.

**Whenever anything goes wrong:** Export tab → **Send report**, and pick
email, Drive or Quick Share. It's one zip with everything: device and USB
details, every app and receiver event with timestamps, the RTL-SDR
driver's own messages, signal statistics, why the app last exited
(including crashes), the latest session, and its recording if you made
one. That is usually enough to find the cause without guessing.

## What you need

- RTL-SDR Blog V3
- An HF antenna on the V3's SMA input. A random wire or dipole for 40 m is
  enough. Direct sampling is not a substitute for an antenna.
- USB-C OTG adapter, or a powered USB-C hub (try both; see step 6)
- Galaxy Tab S7 FE (SM-T733)
- If the Play Store "RTL-SDR driver" app is installed, make sure it isn't
  running, and pick **GhostNet Monitor** when Android asks which app should
  open the dongle.

## The app at a glance

| Tab | What it's for |
|---|---|
| Receive | Mode (JS8 / RTTY / ALE (2G) / Voice SSB / Voice FM; VARA and others show Coming Soon), frequency preset, Start/Stop, status lines, live waterfall, and the mode's output: messages, RTTY text, or live audio |
| Schedule | Every GhostNet V1.5 window in Florida time and UTC, region filters. Tap a window to select its frequency and mode (ALE windows select the 10-channel scan). VARA and others are listed as Coming Soon. |
| Export | **Send report**, past sessions (view, or export JSON / CSV / TXT), IQ recordings |
| Settings | Find receiver, Invert spectrum, RTL AGC, PPM, IQ recording, verbose decoder log, **Run self-test**, session retention, about and licences |

## Step 0: install

Sideload `GhostNet_Monitor_0.5_debug.apk`, or build it:

```bash
cd android
./gradlew :app:assembleDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

Building needs JDK 17, Android SDK platform 35, NDK `25.1.8937393` and
CMake `3.22.1`. CI also builds it on every push; the APK is attached to
each run as an artifact.

## Step 1: self-test (no radio)

Settings → **Run self-test**. It runs a built-in synthetic JS8
transmission, in the exact format the RTL-SDR delivers, through the full
demodulator and decoder on the tablet. It takes about 20 seconds.

- **PASS** with `N0CALL: @GHOSTNET SELF TEST` means the native code works on
  this tablet. Note the "x real time" figure: it should be about 3–4 (the
  test is paced at 4x), and it must be well above 1.
- **FAIL** means a software problem on this device. Send report, and stop
  here.

## Step 2: connect the receiver

Plug the V3 into the tablet. When Android asks which app should open it,
choose GhostNet Monitor; tick "always" if offered. On Receive, the
**Device** line should read `OK ... ready`. If it shows
`CHECK ... tap Find receiver`, go to Settings → **Find receiver / grant USB
access**. If the device isn't seen at all, try the other USB route (direct
OTG vs. powered hub).

## Step 3a: quickest RF check, FM weather radio

Receive → **Voice (FM)** → **TEST · NOAA Weather 162.550 MHz** (try 162.400
or 162.475 if that's silent where you are) → **START LISTENING**. You
should hear the weather broadcast within seconds, through the speaker or
headphones. This proves the dongle, USB and audio path end to end with
the dongle's normal tuner. It doesn't exercise HF direct sampling; step 3b
does that. Nothing from voice modes is recorded or logged.

## Step 3b: receive on a busy JS8 frequency

On Receive, pick **TEST · JS8 calling 7.078 MHz** (evening/night) or
**TEST · JS8 calling 14.078 MHz** (daytime). These aren't GhostNet
frequencies; they carry JS8 traffic most hours, so they prove reception
without waiting for a scheduled net.

Optional but useful: Settings → **Record raw IQ during sessions**, 10 min.
If nothing decodes, that recording lets me replay exactly what your
receiver saw.

Tap **START RECEIVING** and watch:

| Line | Good looks like | If not |
|---|---|---|
| Device | `OK Receiving · RTL-SDR ... direct sampling 2` | `direct sampling 0` below 24 MHz means Q-branch mode didn't engage: send report |
| Signal | `OK Samples flowing · IQ about -20 to -45 dBFS` | `No samples`: USB or power problem. `Very weak`: antenna. `Overloaded`: turn RTL AGC off, or use a shorter antenna |
| Waterfall | Noise texture, with vertical traces about 50 Hz wide (JS8 signals, usually 500–2500 Hz) coming and going every 15 s | Flat black or a single unchanging line: no antenna signal reaching the decoder |
| Decoder | Frames within a few minutes; yellow ticks appear on the waterfall where decodes happen | See below |
| Clock | `timing good` | `Signals arrive ±x s off the slot`: turn on automatic date and time |

Give it 15 minutes before concluding anything; JS8 activity comes and goes.

### Signal OK, traces on the waterfall, nothing decodes

1. **Invert spectrum.** Stop, Settings → turn it on, start again. This is
   the most likely cause and the one thing I couldn't verify without
   hardware: whether Q-branch direct sampling flips the spectrum. With the
   wrong setting the decoder gets nothing at all.
2. **RTL AGC.** Try the other setting.
3. **Clock.** A tablet more than about 2 s off UTC decodes nothing.
4. Still nothing: **Send report** and include the recording when asked.

## Step 3c: RTTY and LSB voice

- **RTTY** (7.077 MHz): RTTY isn't on the air continuously, so this mostly
  waits for the GhostNet window (see Step 4). The Decoder line shows
  "Searching for an RTTY tone pair" until it locks onto a signal, then
  text appears under "Receiving now". `~` marks a character with a
  framing error. If tones lock but the text is garbage, turn on
  **Reverse** in Settings; if it never locks on a visible signal, try the
  425 / 850 Hz shifts.
- **LSB voice** (7.190 MHz): emergency-only, so usually silent. Tuning
  there and hearing band noise confirms the path works.

## Step 3d: 2G ALE

Receive → **ALE (2G)** → **ALE · scan all 10 GhostNet channels** → START
RECEIVING. The Decoder line shows `Scanning · now x.xxxx MHz (channel n)`
and should step through the list about once a second. Check this first:
if the channel never changes, or the line says the receiver refused to
change channel, send a report. That is the part of ALE I could not test
without the dongle, and the 28.1085 MHz channel additionally makes the
driver switch out of direct sampling and back on every pass.

Hearing GhostNet ALE needs someone sounding or calling, so it mostly waits
for an ALE window on the Schedule tab. To check ALE decoding at any time,
use **TEST · amateur ALE 14.109 MHz** (daytime) or **7.102 MHz** (night).
These are amateur HFLink channels where stations send soundings around the
clock, roughly every hour per station, so allow 15–30 minutes. ALE sounds like a fast warbling
burst, 8 tones between 750 and 2500 Hz; on the waterfall it's a block about
1.75 kHz wide.

Results: **Calls & soundings** shows e.g. `Sounding from KX4ABC (TIS)` or
`N0CALL calling GHOSTNET · AMD: …`; **Raw words** shows every decoded word
with its channel and Golay correction count. If the waterfall shows ALE
bursts but nothing decodes, try **Invert spectrum** (same reason as JS8) and
send a report. Scanning doesn't record IQ; for a recording, use a single-channel
ALE preset with recording on.

## Step 4: GhostNet windows

Once step 3b decodes, use the Schedule tab: it shows the next window and
selects the frequency and mode when you tap it. The North America
regional net is Thursday from 8:00 PM EST / 9:00 PM EDT (Friday 01:00 UTC):
JS8 on 7.107 MHz, then RTTY on 7.077 at +60 min and LSB voice on 7.190 at
+90 min (VARA at +30 min is Coming Soon). Saturday bridges use 14.107 and
3.575 MHz. **GhostNet groups only** on
Receive hides other traffic from the display; everything decoded is still
stored.

## Step 5: endurance and failure handling

- **Screen off.** Start receiving, lock the tablet for 15 minutes, unlock.
  The frame count should have kept rising, and the notification should
  stay up the whole time.
- **Unplug while receiving.** The session should end as `Interrupted
  (device_removed)` (shown on the Export tab), and the app should show the
  error without crashing.
- **Power.** Note whether the tablet charges or drains through a powered
  hub during a long session, and any heat.
- **One-hour run.** Battery at start and end, and whether the Signal line
  ever reports dropped samples.

## What to send back

1. **Send report** (Export tab), after any test, good or bad. Include the
   recording if nothing decoded.
2. Which **Invert spectrum** setting decoded (this settles the open question).
3. For ALE: whether scanning stepped through all 10 channels, and any
   decoded call or sounding (or a recording of bursts that didn't decode).

## Step 6 (optional): desktop check with `ghostnet-rx`

Only needed if the tablet fails at USB and you want to rule out the dongle
and antenna. On Linux (macOS untested):

```bash
cd rx-core
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/ghostnet-rx --preset test40 --record test40.cu8
```

Recordings from the tablet replay the same way:
`./build/ghostnet-rx --iq-file GNM_....cu8`.
