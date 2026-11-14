# GhostNet Monitor — Android Technical Specification

**Version:** 1.0  
**Status:** Implementation Baseline  
**Date:** 13 November 2026  
**Platform:** Android • Receive-Only

| **DOCUMENT PROFILE**    | **SPECIFIED BASELINE**                                                     |
|-------------------------|----------------------------------------------------------------------------|
| Target device           | Samsung Galaxy Tab S7 FE Wi-Fi (SM-T733), 4 GB RAM                         |
| Platform                | Android application (Kotlin / Jetpack Compose; native decoder integration) |
| RF interfaces           | RTL-SDR Blog V3; DigiRig Mobile; DigiRig Lite                              |
| Initial reception modes | JS8, analog voice (HF LSB + VHF/UHF FM), RTTY, 2G ALE                      |
| Deferred modes          | VARA HF, Winlink P2P decode, DMR, GhostMesh                                |
| Navigation              | Receive / Schedule / Export / Settings                                     |
| Source schedule         | S2 Underground, GhostNet Version 1.5 (pages 12–18, 30, 33–34)              |
| Audience                | Android developers, radio integration engineers, QA testers                |

This document is an implementation specification, not an assertion that an Android application has already been built or that third-party decoder integrations have passed device testing.

The reader is assumed to understand GhostNet. The document therefore concentrates on software behavior, frequency assignments, scheduled receive windows, signal processing, hardware compatibility, data storage, and release acceptance.

# Document control and reading guide

| **Item**             | **Definition**                                                                          |
|----------------------|-----------------------------------------------------------------------------------------|
| Specification ID     | GNM-ANDROID-SPEC-1.0                                                                    |
| Status               | Approved design direction / engineering implementation pending                          |
| Primary source       | GhostNet Version 1.5 (S2 Underground), used without redefining its published plan       |
| Time convention      | Published schedule in UTC. Local displays in America/New_York with DST adjustment.      |
| Regulatory posture   | Receive-only by design. No software-initiated transmission, keying, or acknowledgement. |
| Availability wording | A protocol is selectable only after end-to-end validation in GhostNet Monitor.          |

## Contents

1\. Purpose, boundaries, and release rules

2\. Operator workflows and operational states

3\. Hardware, signal sources, and RF control

4\. Protocol decoder implementation requirements

5\. Complete booklet frequency catalog

6\. Weekly GhostNet schedules and schedule engine

7\. Android navigation and interface specifications

8\. Sessions, message data, local storage, and export

9\. Application architecture and integration contracts

10\. Receive-only safety, permissions, and resilience

11\. Development phases, verification, and acceptance

12\. Open technical issues and source references

Appendix A. Example session record and error states

Appendix B. Quick-use operating matrix

## Terms used in this specification

| **Term**       | **Meaning**                                                                                                                          |
|----------------|--------------------------------------------------------------------------------------------------------------------------------------|
| Target V1 mode | A mode selected for active engineering integration in the first release; not automatically available now.                            |
| Available      | The actual app, receiver, and decoder chain have passed the specified acceptance tests.                                              |
| Coming Soon    | The mode is visible but greyed out, nonselectable, and cannot start a monitoring session.                                            |
| Session        | One explicitly started, continuously monitored digital-mode interval, ending on Stop, mode/frequency change, disconnect, or failure. |
| Dial frequency | Radio/SDR tuned carrier-reference frequency; separate from an audio waterfall offset.                                                |
| Receive-only   | No application path that sends modulation audio, asserts PTT, issues TX CAT commands, or auto-responds.                              |

# 1. Purpose, boundaries, and release rules

## 1.1 Product objective

Build a single offline-capable Android program that accepts incoming RF or demodulated radio audio, selects a GhostNet frequency and protocol, renders genuine received information, and preserves a separate searchable log for each digital monitoring session. All modes and regions share one consistent four-tab interface.

## 1.2 Locked design decisions

- Android tablet is the only active development and test platform. No Windows, Linux, macOS, or iOS application is in the V1 workload.

- The bottom navigation contains exactly four primary tabs: Receive, Schedule, Export, Settings.

- Receive has independent region and signal-type selectors. Region choices: All, North America, Europe, Australia, and Data Bridges.

- Three supported input categories are shown in Settings: SDR, DigiRig Mobile, and DigiRig Lite.

- Initial engineering modes are JS8, analog voice, RTTY, and 2G ALE. All other booklet modes appear as Coming Soon.

- Every digital monitoring session is stored separately, including completed sessions with zero decodes.

- Analog audio is never recorded and creates no saved audio log or voice-session transcript.

- Offline reception, schedule viewing, local logs, and export work without internet.

- Existing protocol implementations are reused and appropriately licensed; decoders are not reinvented.

## 1.3 Non-goals and deferred scope

- No RF transmission, amateur radio QSO management, voice recorder, transcription service, autoreplies, Winlink mailbox, Internet gateway, or network message relay.

- No claim of native VARA decoder or passive Winlink mail interception.

- No simultaneous cross-band reception from a single narrowband receiver.

- No automatically trusted emergency intelligence: decoded messages may be unauthenticated or incomplete.

- No automated transmission-based checking such as on-air SNR requests or heartbeats.

## 1.4 Release availability gate

A mode is NOT enabled merely because compatible open-source code exists. It becomes Available only after a real input -\> demodulation -\> decoder -\> UI -\> log test passes on the reference Android tablet. Any otherwise unsupported or incomplete mode uses the exact greyed-out caption “Coming Soon”. Target V1 modes remain gated until they satisfy that rule.

# 2. Operator workflows and operational states

## 2.1 First-run device setup

1.  Open Settings and choose SDR, DigiRig Mobile, or DigiRig Lite.

2.  Grant only the Android USB or audio permissions necessary for the selected interface.

3.  For SDR: confirm receiver hardware, HF sampling method, antenna path, gain, and frequency correction.

4.  For DigiRig: select the USB audio input, identify the connected radio, and choose whether supported receive-only CAT is available.

5.  Use Test Input to verify audio or IQ arrival; a live sample stream must not be mislabeled a successful digital decode.

6.  Select optional local export destination through Android’s system folder picker. Local session storage works without a destination.

## 2.2 Digital receive workflow

7.  Open Receive and select region, protocol, and a scheduled or manual preset.

8.  Confirm the configured input source, actual tuned frequency, sideband, and decoder health.

9.  For DigiRig Lite, manually tune the external radio as instructed and acknowledge the frequency check.

10. Press Start Receiving. Create one new digital session in the local database.

11. Display live decoder messages with UTC timestamps, parsed metadata, and raw text. Persist records as decoding occurs.

12. Press Stop, change mode/frequency, lose the device, or exit. Finalize that session with an explicit end reason.

13. Review and export that session from Export.

## 2.3 Analog receive workflow

Select an analog preset (7.190 MHz LSB for regional emergency voice, or the published VHF/UHF analog FM frequencies for unscheduled line-of-sight listening), start audio monitoring, listen on tablet speaker/headphones, and stop. Do not create a message log, session log, audio file, or transcript. Temporary system audio buffers are permitted solely for live playback.

## 2.4 Session control states

> IDLE -\> CONNECTING -\> READY -\> RECEIVING -\> STOPPING -\> IDLE  
> \| \|  
> +-- ERROR \<---+ (session finalized if one began)

Ready means the source is initialized and configured, not that a valid signal was decoded. The interface must show separate statuses for hardware connection, sample/audio reception, and successful decoder output.

# 3. Hardware, signal sources, and RF control

## 3.1 Reference Android hardware

| **Component**     | **Required or assumed**                                                                                                  |
|-------------------|--------------------------------------------------------------------------------------------------------------------------|
| Tablet            | Samsung Galaxy Tab S7 FE Wi-Fi, SM-T733, 4 GB RAM; Android USB host support                                              |
| Connection        | Compatible USB-C OTG/data cable or powered USB hub; sufficient power during extended receive sessions                    |
| Radio/SDR antenna | Operator-supplied antenna suitable for the selected band; HF direct sampling is not a substitute for a usable HF antenna |
| Storage           | App-private local database, plus optional Android Storage Access Framework export destination                            |
| Clock             | Accurate system time for JS8; offline time drift monitoring and approved manual/decoder-assisted correction if supported |

## 3.2 Source capability matrix

| **Capability**      | **RTL-SDR Blog V3**      | **DigiRig Mobile**                    | **DigiRig Lite**                                |
|---------------------|--------------------------|---------------------------------------|-------------------------------------------------|
| Signal supplied     | Raw IQ samples           | Radio receive audio via USB           | Radio receive audio via USB                     |
| HF demodulation     | App must perform DSP     | External radio demodulates            | External radio demodulates                      |
| Tune control        | App tunes SDR            | CAT receive-only when supported       | Manual on radio                                 |
| USB components      | SDR USB device           | USB audio + serial bridge             | USB audio device                                |
| Frequency certainty | Read from SDR driver     | Verify via read-only CAT if available | Operator-confirmed; not electronically verified |
| Applicable V1 modes | JS8 / RTTY / ALE / voice | JS8 / RTTY / ALE / voice              | JS8 / RTTY / ALE / voice, manually retuned      |

DigiRig Mobile includes a USB sound card and separate serial bridge, whereas DigiRig Lite supplies audio but no CAT interface. Some DigiRig cables expose PTT conductors even when the app does not use them. No hardware or software TX assumptions are permitted.

## 3.3 SDR specifics

- Use RTL-SDR Blog V3 HF direct sampling (Q branch or equivalent verified driver mode) for lower HF bands; explicitly validate sampling/tuning on Android.

- Allow frequency selection, PPM correction, gain control where supported, and app demodulation to filtered mono audio for digital decoders.

- Handle 28.1085 MHz ALE carefully: it is near the RTL-SDR V3 direct-sampling/tuner transition; confirm receiver mode and real sensitivity on hardware rather than promising identical performance.

- Use the RTL-SDR tuner path (not HF direct sampling) when monitoring VHF/UHF analog FM. On one dongle, only one RF slice is received at a time; do not claim simultaneous HF and VHF/UHF monitoring.

## 3.4 DigiRig specifics and radio tuning

- Expose radio model, sound-card identity, CAT transport and configured baud rate for DigiRig Mobile only.

- Use read-only CAT queries and receive-frequency changes where separately proven safe for the chosen radio and interface.

- Never use CAT PTT commands, RTS/DTR hardware PTT or VOX-triggering output audio. Never transmit test tones.

- With DigiRig Lite, show frequency + USB/LSB instructions, let the operator acknowledge manual tuning, then decode incoming radio audio.

- If CAT cannot be established, fall back to manual tuning without claiming radio-frequency verification.

- USB connect/disconnect must trigger visible status and safely close a digital session.

## 3.5 Input and decoder audio contract

Normalize each receiver to an internal stream carrying sample rate, format, tuned frequency if known, USB/LSB selection, measured level, source ID, monotonic capture timestamp and UTC observation time. Native modules may request different rates; resampling must be explicit and tested, never assumed. The output used by decoding libraries is receive audio only.

# 4. Protocol decoder implementation requirements

## 4.1 JS8 — initial target

Backend: js8core, an independent MIT-licensed C++20 implementation of the JS8 modem and decoder (vendored in `third_party/js8core`), wrapped by the first-party `rx-core` receive pipeline and an Android JNI bridge. *(Revised 2026-11-14: the original baseline named the GPLv3 JS8Call-improved/Android-port. That port was evaluated in Phase 0 and is not used.)* js8core's engine still contains transmit code, so the GhostNet Monitor build must keep transmit paths unreachable at every layer, not merely hide a button. In the current implementation the engine is created with no audio output, rig control or network dependency, no transmit method is called, and the JNI surface has no transmit entry point. The decoder carries no GPL obligation; the RTL-SDR driver (rtl-sdr-blog, GPLv2) does. See section 9.5.

- GhostNet dial presets: 3.575, 7.107 and 14.107 MHz (USB). The GhostNet 7.107/14.107 presets are not silently replaced by standard JS8Call defaults.

- Show multiple actual decoded transmissions per passband when supported by the integrated decoder; preserve audio offset separately from dial frequency.

- Parse station/callsign, directed group, content, decode time, available signal metrics and multi-frame message assembly status.

- Offer filter chips for @GHOSTNET, @GSTFLASH, and region identifiers (for example @GNUSAFL). Filtering must NOT suppress raw storage of other successfully decoded traffic.

- Warn when time accuracy may prevent decoding. Offline operation remains a requirement; reception-derived drift adjustment is acceptable if validated.

- No heartbeat generation, SNR requests, acknowledgements, replies, CQ, PTT, transmit audio, or spot uploads.

## 4.2 Analog voice — initial target

Provide live audio monitoring through the Android audio output path. On SDR, implement verified USB/LSB/FM demodulation as applicable. On DigiRig, capture audio already demodulated by the radio. The published 7.190 MHz GhostNet voice fallback uses LSB. The booklet also specifies unscheduled VHF analog FM (USA/Australia/Pacific 146.105 MHz; Europe/Africa 144.605 MHz) and UHF analog FM (USA/Australia/Pacific 446.105 MHz; Europe/Africa 437.105 MHz). These FM channels are part of the V1 analog target. Tune SDRs through their VHF/UHF tuner, or receive via an appropriately capable external radio. AM and unrelated modes remain outside initial GhostNet analog scope. No recording, transcription, voice session log, or persisted received content.

## 4.3 RTTY — initial target

Backend: reuse fldigi-derived receive decoder implementation, preferably the established fldigi code; compare the Rust port in the Nexus project if Android native integration is materially easier. Android/NDK build, codec licensing, and real 45.45-baud decode must be validated before enabling this mode.

- GhostNet preset: 7.077 MHz, 45.45 baud; USB receive demodulation for SDR or correct external-radio audio routing.

- Allow 170 Hz shift as a provisional engineering default with adjustable mark/space reversal and shift, because the booklet states baud rate but does not specify a shift.

- Preserve raw decoded characters, printable text, decoding confidence/quality where available, and session metadata; do not infer missing characters.

- Handle baudot letters/figures state and decoding errors without silently changing message meaning.

## 4.4 2G ALE / Ion2G-compatible monitoring — initial target

Backend candidate: PC-ALE 2.0 C++17 2G ALE core, with gr-ale as reference/fallback. The upstream PC-ALE repository reports protocol tests but leaves audio/SDR integrations unfinished; this must be treated as integration work, not Android readiness.

- Load all ten published GhostNet ALE USB frequencies exactly as listed in Section 5.

- Implement receive-only 2G ALE soundings, addressed call events and any supported decoded text; classify unsupported payloads honestly.

- Use a channel-scan controller with configurable dwell and scan cycle; document that one tuner only hears one selected channel at a time.

- Automatic scanning is possible on RTL-SDR or a separately validated read-only CAT interface; DigiRig Lite supports manual frequency-step prompts only.

- Log scan context (channel, frequency, observed time) so ALE events can be attributed to the actual channel.

- Do not claim Ion2G network interoperability merely from successful decoding of generic 2G ALE frames; verify with relevant recordings and live traffic.

## 4.5 Deferred protocols — Coming Soon

| **Signal type** | **V1 behavior and reason**                                                                                                                                     |
|-----------------|----------------------------------------------------------------------------------------------------------------------------------------------------------------|
| VARA HF         | Show schedule and 7.107 MHz tuning information; receive start disabled for VARA decode. No verified embeddable Android-native VARA modem.                      |
| Winlink P2P     | Show schedule and radio setup reference only. No mailbox client, no transmission and no promise of passively recovering third-party mail.                      |
| DMR             | Show DMR and encrypted DMR channels as Coming Soon; DMR baseband decoding is not included. Unencrypted VHF/UHF analog FM belongs to the initial analog target. |
| GhostMesh       | Show 915 MHz / LongFast / slot 20 reference only; separate LoRa/Meshtastic hardware stack required.                                                            |

**IMPLEMENTATION NOTE** Only four modes are selected for first-release integration. They remain disabled until actually proven. The deferred modes must always appear as greyed-out “Coming Soon” in the initial UI.

# 5. Complete booklet frequency catalog

The tables below reproduce every distinct specific frequency in GhostNet Version 1.5 relevant to this specification, with source pages. The booklet lists 26 distinct exact frequencies, plus the 915 MHz Meshtastic band reference. All frequencies are receive references only; no transmit authority is conveyed.

## 5.1 HF JS8, RTTY, Winlink and analog voice

| **Frequency (MHz)** | **Band / mode**    | **Purpose**                                         | **Booklet page** |
|---------------------|--------------------|-----------------------------------------------------|------------------|
| 3.575               | 80m / JS8, USB     | International bridges (NA-AUS, EUR-AUS)             | 14, 16, 18       |
| 7.077               | 40m / RTTY         | Regional recovery window, 45.45 baud                | 13, 15, 17       |
| 7.107               | 40m / JS8, USB     | Thursday regional nets; AUS-PAC bridge              | 13–18            |
| 7.107               | 40m / VARA HF, USB | Published Winlink P2P regional window (Coming Soon) | 13, 15, 17       |
| 7.190               | 40m / LSB voice    | Emergency-only fallback                             | 13, 15, 17       |
| 14.107              | 20m / JS8, USB     | Saturday intercontinental bridge windows            | 14, 16, 18       |

Important distinction: 7.107 MHz appears twice because it is used for different modes in separate windows; it is one unique frequency. Frequency alone does not identify the appropriate decoder.

## 5.2 Ion2G / 2G ALE channel list (all USB; page 30)

| **Channel** | **Frequency (MHz)** | **Receive mode** | **Status in booklet** |
|-------------|---------------------|------------------|-----------------------|
| 01GHOSTNET  | 3.5985              | 2G ALE / USB     | Working-draft channel |
| 02GHOSTNET  | 7.1045              | 2G ALE / USB     | Working-draft channel |
| 03GHOSTNET  | 7.1065              | 2G ALE / USB     | Working-draft channel |
| 04GHOSTNET  | 7.1085              | 2G ALE / USB     | Working-draft channel |
| 05GHOSTNET  | 10.1285             | 2G ALE / USB     | Working-draft channel |
| 06GHOSTNET  | 14.1115             | 2G ALE / USB     | Working-draft channel |
| 07GHOSTNET  | 14.1135             | 2G ALE / USB     | Working-draft channel |
| 08GHOSTNET  | 14.1155             | 2G ALE / USB     | Working-draft channel |
| 09GHOSTNET  | 18.1085             | 2G ALE / USB     | Working-draft channel |
| 10GHOSTNET  | 28.1085             | 2G ALE / USB     | Working-draft channel |

The author labels this ALE list a “Working Draft” and warns that it is experimental. Store the source version and the fact that ALE assignments may change; do not silently update the frequencies from an unrelated public ham ALE list.

## 5.3 VHF/UHF line-of-sight reference (page 33)

| **Mode**          | **United States (MHz)** | **Europe (MHz)** | **Initial UI**   |
|-------------------|-------------------------|------------------|------------------|
| VHF DMR           | 145.105                 | 145.105          | Coming Soon      |
| VHF analog FM     | 146.105                 | 144.605          | V1 analog target |
| VHF encrypted DMR | 147.105                 | 145.605          | Coming Soon      |
| UHF DMR           | 445.105                 | 436.105          | Coming Soon      |
| UHF analog FM     | 446.105                 | 437.105          | V1 analog target |
| UHF encrypted DMR | 447.105                 | 438.105          | Coming Soon      |

The booklet assigns ordinary DMR color code 7, timeslot 1, and group-call ID 1105. Encrypted entries are a hypothetical nonstandard plan and are not legal amateur-band encryption in the United States. They must never be shown as enabled operating options. Unencrypted analog FM frequencies have no weekly time window in this booklet and belong under manual Receive presets rather than fabricated schedule entries. Australia/Pacific uses the USA list; Africa uses the Europe list according to the booklet.

## 5.4 GhostMesh reference (page 34; deferred)

U.S. Meshtastic band: 915 MHz (band reference, not a single exact dial frequency). Configuration reference: LongFast, frequency slot 20, channel name GhostMesh. This application will not generate or import Meshtastic channel keys or operate a LoRa radio in V1.

# 6. Weekly GhostNet schedules and schedule engine

The following tables are implementation data, not examples. UTC is authoritative and follows GhostNet Version 1.5 pages 13–18. EST is winter Eastern Standard Time (UTC-5); EDT is summer Eastern Daylight Time (UTC-4). Android must use the America/New_York time zone and a time-zone-aware recurrence engine.

## 6.1 Regional Thursday nets

### Australia - regional Thursday

<table style="width:100%;">
<colgroup>
<col style="width: 24%" />
<col style="width: 23%" />
<col style="width: 23%" />
<col style="width: 29%" />
</colgroup>
<thead>
<tr class="header">
<th><strong>UTC</strong></th>
<th><strong>Florida EST</strong></th>
<th><strong>Florida EDT</strong></th>
<th><strong>Program / frequency</strong></th>
</tr>
</thead>
<tbody>
<tr class="odd">
<td>07:00–07:30 Thu UTC</td>
<td>2:00 AM–2:30 AM</td>
<td>3:00 AM–3:30 AM</td>
<td>JS8Call<br />
7.107 MHz</td>
</tr>
<tr class="even">
<td>07:30–08:00 Thu UTC</td>
<td>2:30 AM–3:00 AM</td>
<td>3:30 AM–4:00 AM</td>
<td>Winlink P2P / VARA<br />
7.107 MHz</td>
</tr>
<tr class="odd">
<td>08:00–08:30 Thu UTC</td>
<td>3:00 AM–3:30 AM</td>
<td>4:00 AM–4:30 AM</td>
<td>RTTY (45.45 baud)<br />
7.077 MHz</td>
</tr>
<tr class="even">
<td>08:30–09:00 Thu UTC</td>
<td>3:30 AM–4:00 AM</td>
<td>4:30 AM–5:00 AM</td>
<td>LSB voice (emergency)<br />
7.190 MHz</td>
</tr>
</tbody>
</table>

### Europe - regional Thursday

<table style="width:100%;">
<colgroup>
<col style="width: 24%" />
<col style="width: 23%" />
<col style="width: 23%" />
<col style="width: 29%" />
</colgroup>
<thead>
<tr class="header">
<th><strong>UTC</strong></th>
<th><strong>Florida EST</strong></th>
<th><strong>Florida EDT</strong></th>
<th><strong>Program / frequency</strong></th>
</tr>
</thead>
<tbody>
<tr class="odd">
<td>18:00–18:30 Thu UTC</td>
<td>1:00 PM–1:30 PM</td>
<td>2:00 PM–2:30 PM</td>
<td>JS8Call<br />
7.107 MHz</td>
</tr>
<tr class="even">
<td>18:30–19:00 Thu UTC</td>
<td>1:30 PM–2:00 PM</td>
<td>2:30 PM–3:00 PM</td>
<td>Winlink P2P / VARA<br />
7.107 MHz</td>
</tr>
<tr class="odd">
<td>19:00–19:30 Thu UTC</td>
<td>2:00 PM–2:30 PM</td>
<td>3:00 PM–3:30 PM</td>
<td>RTTY (45.45 baud)<br />
7.077 MHz</td>
</tr>
<tr class="even">
<td>19:30–20:00 Thu UTC</td>
<td>2:30 PM–3:00 PM</td>
<td>3:30 PM–4:00 PM</td>
<td>LSB voice (emergency)<br />
7.190 MHz</td>
</tr>
</tbody>
</table>

### North America - Thursday evening / Friday UTC

<table style="width:100%;">
<colgroup>
<col style="width: 24%" />
<col style="width: 23%" />
<col style="width: 23%" />
<col style="width: 29%" />
</colgroup>
<thead>
<tr class="header">
<th><strong>UTC</strong></th>
<th><strong>Florida EST</strong></th>
<th><strong>Florida EDT</strong></th>
<th><strong>Program / frequency</strong></th>
</tr>
</thead>
<tbody>
<tr class="odd">
<td>01:00–01:30 Fri UTC</td>
<td>8:00 PM–8:30 PM</td>
<td>9:00 PM–9:30 PM</td>
<td>JS8Call<br />
7.107 MHz</td>
</tr>
<tr class="even">
<td>01:30–02:00 Fri UTC</td>
<td>8:30 PM–9:00 PM</td>
<td>9:30 PM–10:00 PM</td>
<td>Winlink P2P / VARA<br />
7.107 MHz</td>
</tr>
<tr class="odd">
<td>02:00–02:30 Fri UTC</td>
<td>9:00 PM–9:30 PM</td>
<td>10:00 PM–10:30 PM</td>
<td>RTTY (45.45 baud)<br />
7.077 MHz</td>
</tr>
<tr class="even">
<td>02:30–03:00 Fri UTC</td>
<td>9:30 PM–10:00 PM</td>
<td>10:30 PM–11:00 PM</td>
<td>LSB voice (emergency)<br />
7.190 MHz</td>
</tr>
</tbody>
</table>

Regional nets: monitoring 7.190 MHz during the final five minutes of each communications window is recommended by the booklet. On a single receiver this would interrupt the other active mode. V1 must not switch without an explicit user request; UI may show a 5-minute voice reminder.

## 6.2 Saturday data bridges

### Australia - South Pacific bridge

<table style="width:100%;">
<colgroup>
<col style="width: 24%" />
<col style="width: 23%" />
<col style="width: 23%" />
<col style="width: 29%" />
</colgroup>
<thead>
<tr class="header">
<th><strong>UTC</strong></th>
<th><strong>Florida EST</strong></th>
<th><strong>Florida EDT</strong></th>
<th><strong>Program / frequency</strong></th>
</tr>
</thead>
<tbody>
<tr class="odd">
<td>08:00–09:00 Sat UTC</td>
<td>3:00 AM–4:00 AM</td>
<td>4:00 AM–5:00 AM</td>
<td>JS8Call<br />
14.107 MHz</td>
</tr>
<tr class="even">
<td>09:00–09:30 Sat UTC</td>
<td>4:00 AM–4:30 AM</td>
<td>5:00 AM–5:30 AM</td>
<td>JS8Call<br />
7.107 MHz</td>
</tr>
<tr class="odd">
<td>09:30–10:00 Sat UTC</td>
<td>4:30 AM–5:00 AM</td>
<td>5:30 AM–6:00 AM</td>
<td>Ion2G 2G ALE<br />
10-channel list</td>
</tr>
</tbody>
</table>

### North America - Australia bridge

<table style="width:100%;">
<colgroup>
<col style="width: 24%" />
<col style="width: 23%" />
<col style="width: 23%" />
<col style="width: 29%" />
</colgroup>
<thead>
<tr class="header">
<th><strong>UTC</strong></th>
<th><strong>Florida EST</strong></th>
<th><strong>Florida EDT</strong></th>
<th><strong>Program / frequency</strong></th>
</tr>
</thead>
<tbody>
<tr class="odd">
<td>12:00–13:00 Sat UTC</td>
<td>7:00 AM–8:00 AM</td>
<td>8:00 AM–9:00 AM</td>
<td>JS8Call<br />
14.107 MHz</td>
</tr>
<tr class="even">
<td>13:00–13:30 Sat UTC</td>
<td>8:00 AM–8:30 AM</td>
<td>9:00 AM–9:30 AM</td>
<td>Ion2G 2G ALE<br />
10-channel list</td>
</tr>
<tr class="odd">
<td>13:30–14:00 Sat UTC</td>
<td>8:30 AM–9:00 AM</td>
<td>9:30 AM–10:00 AM</td>
<td>JS8Call<br />
3.575 MHz</td>
</tr>
</tbody>
</table>

### North America - Europe bridge

<table style="width:100%;">
<colgroup>
<col style="width: 24%" />
<col style="width: 23%" />
<col style="width: 23%" />
<col style="width: 29%" />
</colgroup>
<thead>
<tr class="header">
<th><strong>UTC</strong></th>
<th><strong>Florida EST</strong></th>
<th><strong>Florida EDT</strong></th>
<th><strong>Program / frequency</strong></th>
</tr>
</thead>
<tbody>
<tr class="odd">
<td>18:00–19:00 Sat UTC</td>
<td>1:00 PM–2:00 PM</td>
<td>2:00 PM–3:00 PM</td>
<td>JS8Call<br />
14.107 MHz</td>
</tr>
<tr class="even">
<td>19:00–20:00 Sat UTC</td>
<td>2:00 PM–3:00 PM</td>
<td>3:00 PM–4:00 PM</td>
<td>Ion2G 2G ALE<br />
10-channel list</td>
</tr>
</tbody>
</table>

### Europe - Australia bridge

<table style="width:100%;">
<colgroup>
<col style="width: 24%" />
<col style="width: 23%" />
<col style="width: 23%" />
<col style="width: 29%" />
</colgroup>
<thead>
<tr class="header">
<th><strong>UTC</strong></th>
<th><strong>Florida EST</strong></th>
<th><strong>Florida EDT</strong></th>
<th><strong>Program / frequency</strong></th>
</tr>
</thead>
<tbody>
<tr class="odd">
<td>20:00–21:00 Sat UTC</td>
<td>3:00 PM–4:00 PM</td>
<td>4:00 PM–5:00 PM</td>
<td>JS8Call<br />
14.107 MHz</td>
</tr>
<tr class="even">
<td>21:00–21:30 Sat UTC</td>
<td>4:00 PM–4:30 PM</td>
<td>5:00 PM–5:30 PM</td>
<td>Ion2G 2G ALE<br />
10-channel list</td>
</tr>
<tr class="odd">
<td>21:30–22:00 Sat UTC</td>
<td>4:30 PM–5:00 PM</td>
<td>5:30 PM–6:00 PM</td>
<td>JS8Call<br />
3.575 MHz</td>
</tr>
</tbody>
</table>

The regional 7.107 MHz Winlink/VARA windows must appear in Schedule even though decoding is Coming Soon. All ALE windows reference the ten-channel list in Section 5.2, not a single fixed frequency.

## 6.3 Schedule database and recurrence

- Store each event with ID, network/region, UTC weekday, UTC start and end, frequency preset or ALE channel-set reference, receive mode, source booklet version and page.

- The North America Thursday evening net is 01:00–03:00 UTC Friday. The UI must label it Thursday evening when viewed in Florida, avoiding weekday off-by-one errors.

- Local time is derived when displaying events. Never make local times the canonical recurring schedule; daylight-saving transitions must be handled by the OS time-zone database.

- The Schedule tab displays all entries, including Coming Soon modes. Selecting a supported entry can preload its Receive preset; selecting a deferred entry displays details but disables Start Receiving.

- No unattended or automatic scanning/retuning in V1 without the operator enabling it; do not interrupt an active session when a schedule changes.

- The schedule is bundled for offline use, versioned, and replaceable in a future app release. No unverified live feed overwrites the embedded schedule.

## 6.4 Printed-source discrepancies

Australia page 17 lists a VARA session at 0730–0800 UTC, but its narrative says handshakes start at 2035Z. That is contradictory. The implementation uses the explicit 0730–0800 UTC table slot and records the conflicting handshake statement as a source erratum; it does not create an extra 2035Z session.

# 7. Android navigation and interface specifications

## 7.1 Primary navigation

| **Bottom tab** | **Primary purpose**            | **Required content**                                                                                                     |
|----------------|--------------------------------|--------------------------------------------------------------------------------------------------------------------------|
| Receive        | Live operation                 | Region selector; signal type; frequency preset; source and health indicators; mode-specific panel; Start/Stop.           |
| Schedule       | Published operating windows    | Thursday/Saturday calendar; UTC + local conversion; region filters; frequency/mode details; Tune/Preview when supported. |
| Export         | Historical digital sessions    | Session list, search/filter, details, selection, export format, destination and export result.                           |
| Settings       | Hardware and app configuration | Device type, USB audio/SDR or CAT settings, time zone, log retention, export folder, diagnostics, licenses.              |

## 7.2 Receive screen anatomy

- Top strip: connection identity and three independent status indicators (Device, Signal, Decoder).

- Region subtabs: All / North America / Europe / Australia / Bridges. Select region without losing a current digital session unless changing the actual tuned preset.

- Mode subtabs or chips: JS8 / Analog / RTTY / ALE, followed by greyed-out VARA / Winlink / DMR / GhostMesh (“Coming Soon”).

- Tuning card: dial frequency in MHz, band, sideband, audio offset when applicable, preset title, source type and whether tuning is automatic or operator-confirmed.

- Primary control: Start Receiving when configured, Stop when active; never a transmit or reply button.

- Active digital area: chronological decoded message/event cards with timestamp, callsign or source, group, content, optional signal metrics, and empty-state guidance.

- Analog area: audio playback and live level meter with HF LSB and VHF/UHF FM presets, plus persistent “Audio is not recorded or logged” note.

- Always-visible warnings for clock drift, USB removal, unsupported tuning, missing antenna/poor input level, and decoder failures.

## 7.3 Coming Soon interaction contract

Disabled modes remain in the mode selector with reduced contrast and the literal text “Coming Soon”. Tapping a greyed-out mode must never initialize a decoder, start a session, or imply working reception. It may open a short explanation of the missing capability if desired, but no simulated traffic should appear.

## 7.4 Display and accessibility

- Support the tablet in portrait and landscape without clipped controls.

- Provide minimum touch targets, text scaling, dark/light system theme compatibility, and accessible labels.

- Do not rely only on color for health status; include explicit text and icon.

- Decoded text uses selectable monospaced or readable typography and preserved line breaks for actual content.

- Use human-readable local timestamps plus an accessible UTC timestamp in message details.

# 8. Sessions, message data, local storage, and export

## 8.1 Digital session lifecycle

Each Start Receiving action for JS8, RTTY or ALE creates exactly one session record with a unique immutable ID. End the session on explicit Stop, decoder mode change, frequency change requiring retune, source removal, unrecoverable decoder fault, or controlled app shutdown. A forced app crash marks the existing session Interrupted on restart; it must not silently merge with a new session.

| **Session field**             | **Requirement**                                                                              |
|-------------------------------|----------------------------------------------------------------------------------------------|
| session_id                    | Unique ID (UUID recommended).                                                                |
| start_utc / end_utc           | UTC instants; end_utc may be null only while active.                                         |
| region / schedule_event_id    | Region or bridge category, plus optional published event link.                               |
| protocol / decoder_version    | JS8, RTTY, 2G ALE; library version/build identity.                                           |
| receiver_type / receiver_id   | SDR, DigiRig Mobile, or DigiRig Lite; device label where available.                          |
| dial_frequency / sideband     | Actual tuned value if measurable, otherwise operator-confirmed value with verification flag. |
| message_count / decode_errors | Real counts, including zero-message sessions.                                                |
| state / end_reason            | Completed / Interrupted / Failed; stop/disconnect/switch/crash etc.                          |
| clock_health / capture_notes  | Clock confidence, drift notes and operator notes without invented signal data.               |

## 8.2 Decoded message/event record

| **Field**                     | **Requirement**                                                                             |
|-------------------------------|---------------------------------------------------------------------------------------------|
| message_id / session_id       | Immutable primary ID and parent session foreign key.                                        |
| received_utc                  | Timestamp associated with the received frame or message.                                    |
| raw_text / parsed_text        | Exact decoder text separate from optional parsed/displayed text.                            |
| source_callsign / destination | Only as reliably decoded; null when unknown.                                                |
| group_id / priority_marker    | Group identifier as received; @GSTFLASH is a received label, not verified emergency status. |
| freq_hz / audio_offset_hz     | Measured or configured values with units; optional where unavailable.                       |
| snr / quality / decode_status | Nullable measured decoder fields; preserve fragment/partial state where applicable.         |
| protocol_payload              | Mode-specific non-text information: ALE sounding/address/sequence metadata, etc.            |

## 8.3 Storage rules

- Use Room/SQLite for all digital sessions and messages; one SQLite database with foreign keys and indices is preferable to uncontrolled separate files for each session.

- Session separation is logical in the database and physical in the exported per-session file(s).

- Commit decoded messages promptly and preserve raw decoder output. Do not delete all messages when a session terminates abnormally.

- Deduplicate only with a documented deterministic key; a repeated transmission is not necessarily a duplicate in operational context.

- Voice listening never writes a session row, message row, audio file or waveform history.

- Use UTF-8, UTC instants, clear units, and lossless storage of decoder text.

## 8.4 Export tab behavior

- Show chronological session cards with protocol, region, start/end local time, UTC time in detail, frequency, device, message count and completion state.

- Allow select one/multiple sessions, inspect records, search by callsign/group/text, filter date/mode, and export without modifying the stored originals.

- Offer CSV for tabular records, JSON for full-fidelity records, and human-readable TXT as first-release formats.

- Default filename: GNM_YYYYMMDDTHHMMSSZ\_\<MODE\>\_\<REGION\>\_\<short-id\>.\<ext\> (UTC start time).

- Exports include a metadata header/manifest, session details, message records, data schema version, GhostNet booklet version, and any explicit decoder or clock limitations.

- Let user choose an Android Storage Access Framework folder in Settings; show a standard Save As picker if no writable destination is configured.

- Use crash-safe export through a temporary document or equivalent atomic write; verify completion and report failures without claiming export success.

## 8.5 Voice and mode-specific retention exceptions

Voice sessions are neither logged nor exported. A digital session with no decodes still appears in Export with message_count = 0 and diagnostic session metadata. For ALE, soundings and link events are logged as protocol events even when no ordinary text message exists.

# 9. Application architecture and integration contracts

## 9.1 Proposed module layout

> Android UI (Kotlin / Jetpack Compose)  
> \|-- Receive ViewModel / Schedule ViewModel / Export ViewModel / Settings  
> \|-- Monitor Controller + Capability Registry + Foreground Service  
> \|-- Receiver Adapter Interface  
> \| \|-- RTL-SDR V3: USB IQ -\> HF DSP -\> demodulated receive audio  
> \| \|-- DigiRig Mobile: USB audio \[+ receive-only CAT adapter\]  
> \| \`-- DigiRig Lite: USB audio \[manual radio tuning\]  
> \|-- Decoder Interface  
> \| \|-- JS8Call Android native core / JNI  
> \| \|-- fldigi-derived RTTY receive core  
> \| \|-- PC-ALE 2G ALE receive core / JNI  
> \| \`-- Analog live audio playback (no log sink)  
> \|-- Event Normalizer -\> Room / SQLite -\> Export Service  
> \`-- Versioned GhostNet V1.5 Frequencies + UTC Schedule

## 9.2 Receiver adapter contract

- discoverDevices() / requestUsbPermission() / open() / close()

- getCapabilities(): IQ or audio, tune control, sidebands, sample rates, read-only CAT availability

- setReceiveFrequency(hz) where safe and supported; getReceiveFrequency() only if genuinely available

- startReceiveStream() / stopReceiveStream() with timestamps and channel metadata

- onDisconnect() and onError() callbacks; no transmission methods in the public receiver interface.

## 9.3 Decoder adapter contract

- initialize(config) / start(monoAudio, sampleRate) / stop() / dispose()

- getRequiredSampleRates(), getSettings(), getDecoderVersion(), isAvailable()

- onDecodedEvent(event) with raw content, protocol, timestamp, available confidence/metrics and optional frequency offset

- onDiagnostics(metrics) with buffer underruns, clock drift where supported, decoder stalls and sample quality.

- No sendMessage(), encodeTxAudio(), ptt(), autoReply() or analogous production API surface.

## 9.4 Scheduling and capability registry

Bundle the GhostNet V1.5 plan in a versioned local data file. Each scheduled entry references immutable frequency/mode catalog IDs. A capability registry determines whether a protocol is Available and whether a given source supports it; app UI components must not independently infer availability from a text label or file presence.

## 9.5 Native dependencies, licenses and provenance

Keep source-attribution metadata for each integrated decoder, upstream commit/tag, local patches, build instructions, and test recordings (maintained in `THIRD_PARTY.md`). Current V1 stack: js8core (JS8 decoder) is MIT; rtl-sdr-blog (RTL-SDR driver, locally patched for Android file-descriptor access) is GPLv2; libusb is LGPL-2.1; both are statically linked into the native library, so distributing the APK under GPLv2-compatible terms with source is the expected path. Future modes: fldigi/Nexus RTTY receive code has GPL obligations; PC-ALE is MIT. License compatibility and redistribution obligations must be assessed before distributing the APK. Vendor transmit-enabled upstream packages only after disabling/removing their TX paths.

# 10. Receive-only safety, permissions, and resilience

## 10.1 Hard TX prohibition

- Compile out or block user-facing and internal transmission workflows. Do not rely solely on a hidden transmit button.

- Disable upstream JS8 autorreply, heartbeats, beaconing and PSKReporter upload where bundled.

- Never open a transmit-side PCM sink to DigiRig, assert RTS/DTR for PTT, toggle CM108 GPIO for keying, send CAT TX/PTT commands, or output tone sequences designed to trigger VOX.

- Provide a startup warning to check external radio VOX, CAT, automatic functions and physical wiring; application-side receive-only cannot prevent independent radio misconfiguration.

- Enforce receive-only integration tests using instrumented USB/CAT/PTT mocks and, when practical, physical line monitoring.

## 10.2 Android services and permissions

- Use Android USB host APIs and explicit device permission for attached USB hardware.

- Capture USB audio only with the Android-supported audio APIs and foreground service/permissions appropriate to target SDK behavior.

- Use a visible ongoing notification for active background monitoring and provide a Stop action.

- Request folder access with Android Storage Access Framework; persist only user-granted URI permissions. Do not require broad-storage permission.

- Do not require network access, location sharing, contacts or account access for core operation. If any future diagnostics or updates use Internet, they must be explicit and optional.

## 10.3 Data reliability and privacy

- Do not present decoded GhostNet claims as independently authenticated facts; preserve whether a priority label is radio-originated rather than verified.

- Use app-private storage; only user-requested export exposes logs through Android document permissions.

- Sanitize filenames and exported metadata; never include saved passwords, device secrets, arbitrary contacts or hidden radio identifiers.

- No default cloud synchronization or unattended uploads.

- Do not keep microphone, audio, USB or foreground service resources allocated when monitoring is stopped.

## 10.4 Failure and recovery requirements

| **Condition**               | **Required behavior**                                                                                     |
|-----------------------------|-----------------------------------------------------------------------------------------------------------|
| SDR/DigiRig removed         | Stop capture safely, finalize digital session as Interrupted, show reconnect prompt.                      |
| Audio stops or stays silent | Show Signal/Audio status; decoder remains separate from signal status.                                    |
| Clock inaccurate for JS8    | Explicit warning and guidance; never fabricate a successful decode.                                       |
| Unsupported radio CAT       | Switch to manual tuning, flag frequency as unverified; do not key the radio.                              |
| Insufficient storage        | Warn, preserve what is safely committed, stop recording decoded messages if necessary, do not corrupt DB. |
| Thermal/battery degradation | Expose state; support user-initiated Stop; avoid claiming uninterrupted monitoring.                       |
| App force-closed            | Recover database, mark previous active digital session Interrupted, preserve previous decodes.            |

# 11. Development phases, verification, and acceptance

## 11.1 Work packages and implementation order

| **Phase**             | **Deliverable**                                                                   | **Release gate**                                                      |
|-----------------------|-----------------------------------------------------------------------------------|-----------------------------------------------------------------------|
| A. Android foundation | Four tabs, USB detection, capability registry, persistent schedule catalog        | Correct navigation and frequency/schedule display offline.            |
| B. Receiver adapters  | RTL-SDR IQ + DigiRig Mobile/Lite USB audio; manual/CAT tuning states              | Sample/audio streams and tuning status proven with reference devices. |
| C. JS8                | Existing Android decoder core integrated receive-only; groups and log persistence | Known-good RF capture plus live receive decoding, no TX activation.   |
| D. Analog             | Live 7.190 LSB + VHF/UHF FM listening; no audio record or voice log               | Audible receive signal; storage contains no voice content.            |
| E. RTTY               | fldigi-derived 45.45 baud receive decoder                                         | Known RTTY capture decoded correctly through at least one source.     |
| F. ALE                | PC-ALE receive, ten-channel list, scanning/manual tuning                          | Known ALE captures recognized and attributed to correct channels.     |
| G. Export/reliability | CSV/JSON/TXT, session recovery, extended monitoring                               | No loss of committed messages; output can be reimported/verified.     |

## 11.2 Protocol-by-input test matrix

| **Mode** | **RTL-SDR V3**                     | **DigiRig Mobile**                           | **DigiRig Lite**                       |
|----------|------------------------------------|----------------------------------------------|----------------------------------------|
| JS8      | IQ -\> USB -\> decode              | radio audio -\> decode; CAT optional         | radio audio -\> decode; tune manual    |
| Analog   | IQ -\> LSB or FM audio             | radio RX audio -\> playback                  | radio RX audio -\> playback            |
| RTTY     | IQ -\> USB -\> 45.45 baud          | radio RX audio -\> 45.45 baud                | radio RX audio -\> 45.45 baud          |
| 2G ALE   | IQ -\> USB -\> 10-channel scanning | radio RX audio; manual or validated CAT scan | radio RX audio; manual channel changes |

The matrix identifies intended paths. Each populated combination must be tested before being advertised as fully supported. Device-specific partial availability is valid: a mode may work on SDR and remain unavailable for a DigiRig variant until validated.

## 11.3 Required acceptance tests

- Frequency check: all 26 distinct exact frequencies plus the 915 MHz band reference appear without rounding errors; duplicate uses of 7.107 are separate mode assignments.

- Schedule check: all 12 Thursday regional slots and 11 Saturday bridge slots match printed UTC windows. Assert correct local day and DST conversion using winter/summer fixtures.

- JS8 check: at least one actual clean and one low-SNR receive capture; correct raw text, callsign/group, UTC time and session linkage.

- RTTY check: independently generated 45.45 baud recording, appropriate shift setting, and accurate text with preserved decode errors.

- ALE check: known sounding, call establishment, and multi-channel receive events matched to actual tuned frequency; unsupported payloads not falsely decoded.

- Analog check: audible 7.190 MHz LSB and representative VHF/UHF FM reception through the available receiver path; immediate stop and zero persisted voice sessions or audio artifacts.

- USB check: permission deny, unplug/replug, competing USB device, radio manual tuning and failed CAT response.

- Session check: Start/Stop, zero-message session, mode/frequency change, app crash recovery, session export/reopen.

- Safety check: no PTT, transmit PCM, CAT TX commands, auto-reply or network spot uploads in instrumented test harness.

- Longevity check: target a four-hour foreground receive soak on the SM-T733 with stable database growth and no unhandled crashes; document thermal, battery and resource measurements.

## 11.4 Definition of release-ready

Full V1 acceptance targets all four initial modes on RTL-SDR V3, DigiRig Mobile, and DigiRig Lite, with documented hardware limits (for example, manual tuning on DigiRig Lite). Each specific mode-and-source combination must pass its applicable test matrix or remain disabled as Coming Soon. A build with any required combination unverified is a clearly labeled partial build, not fully accepted V1. All schedule entries remain visible and no simulated decodes are presented as real traffic.

# 12. Open technical issues and source references

## 12.1 Issues requiring engineering verification

- js8core pinned upstream commit (vendored snapshot currently unpinned; see `THIRD_PARTY.md`), and on-device validation of the receive-only JNI integration.

- RTL-SDR V3 USB backend and reliable 28.1085 MHz ALE reception around the direct-sampling/tuner boundary.

- USB audio handling and foreground microphone/service requirements under the actual Android version on SM-T733.

- Fldigi vs Nexus RTTY integration trade-offs and 170 Hz shift choice after GhostNet recordings are available.

- PC-ALE audio input/scan integration, signal-quality validation and compatibility with existing Ion2G stations.

- DigiRig Mobile radio-specific receive-only CAT matrix; DigiRig Lite remains manual tune by definition.

- Log retention policy and export defaults beyond the required session separation and user-selected destination.

## 12.2 Primary published reference

S2 Underground, GhostNet Version 1.5, 50-page booklet. Relevant source pages: 12 (schedule overview); 13 (North America Thursday); 14 (North America bridges); 15 (Europe Thursday); 16 (Europe bridges); 17 (Australia Thursday); 18 (Australia bridges); 19 (operating practices); 27 (JS8 receive troubleshooting); 29–30 (Ion2G and ALE list); 31 (receive-only setup); 33 (VHF/UHF); 34 (GhostMesh); 38 (CrisisNet). Source project: https://github.com/s2underground/GhostNet

## 12.3 Third-party integration references

**js8core, JS8 modem/decoder (MIT), used for V1:** https://github.com/punk-kaos/js8core

**JS8Call Android port (GPLv3), evaluated in Phase 0, not used:** https://github.com/JS8Call-improved/Android-port

**fldigi decoder (GPLv3):** https://github.com/w1hkj/fldigi

**Nexus RTTY derived from fldigi (GPLv3):** https://github.com/kd9taw/Nexus

**PC-ALE 2G ALE core (MIT):** https://github.com/Alex-Pennington/PC-ALE

**gr-ale 2G ALE reference (GPL):** https://github.com/gat3way/gr-ale

**DigiRig model comparison:** https://digirig.net/choosing-digirig-model/

**DigiRig Mobile introduction:** https://digirig.net/getting-started-with-digirig-mobile/

**DigiRig Lite setup:** https://digirig.net/digirig-lite-setup-manual/

**WoAD Winlink client (reference, NOT passive decoder):** https://woad.sumusltd.com/

**RTL-SDR V3 HF reception reference:** https://www.rtl-sdr.com/rtl-sdr-blog-v-3-dongles-user-guide/

# Appendix A. Example session record and error states

The following illustrative data is for schema design only. It is NOT a received GhostNet transmission and must never appear as genuine decoded traffic in the deployed application.

```json
{
"session_id": "example-session-001",
"schema_version": "1.0",
"start_utc": "2026-11-13T01:00:00Z",
"end_utc": "2026-11-13T01:30:00Z",
"region": "North America",
"protocol": "JS8",
"dial_frequency_hz": 7107000,
"sideband": "USB",
"input_type": "RTL_SDR_V3",
"status": "Completed",
"messages": [
{"received_utc": "2026-11-13T01:04:15Z",
"raw_text": "EXAMPLE ONLY - NOT LIVE RADIO",
"source_callsign": null, "group_id": null}
]
}
```

## Operator-facing error messages

| **Condition**    | **Example message**                                                                        |
|------------------|--------------------------------------------------------------------------------------------|
| No source        | Connect a receiver in Settings before starting.                                            |
| Unsupported mode | Coming Soon - this receive mode has not yet been verified on Android.                      |
| Lite manual tune | Tune the radio to 7.107 MHz USB, then confirm to begin.                                    |
| JS8 no decode    | Audio is present, but no valid JS8 messages have been decoded yet. Check time and signal.  |
| ALE scan limit   | Listening to one ALE frequency at a time. Other channels are not monitored simultaneously. |
| Export failed    | Export was not saved. Your original session remains stored on this tablet.                 |

# Appendix B. Quick-use operating matrix

| **When**                  | **What to choose**                        | **Mode / dial frequency**                             | **Saved?** |
|---------------------------|-------------------------------------------|-------------------------------------------------------|------------|
| Thursday - AUS            | Australia regional; scheduled mode        | JS8 7.107 / VARA 7.107 / RTTY 7.077 / LSB 7.190       | If enabled |
| Thursday - EUR            | Europe regional; scheduled mode           | JS8 7.107 / VARA 7.107 / RTTY 7.077 / LSB 7.190       | If enabled |
| Thursday evening - NA     | North America regional; scheduled mode    | JS8 7.107 / VARA 7.107 / RTTY 7.077 / LSB 7.190       | If enabled |
| Saturday bridges          | Select regional bridge in Schedule        | JS8 14.107 / 7.107 / 3.575 as listed; ALE 10 channels | If enabled |
| Anytime crisis monitoring | Manual preset, typically regional JS8     | JS8 7.107 USB (no schedule required)                  | Yes        |
| Voice fallback            | Published emergency-only voice slot       | LSB 7.190                                             | No         |
| VHF/UHF analog            | Manual line-of-sight presets; unscheduled | FM 146.105 / 446.105 USA; 144.605 / 437.105 EUR       | No         |

Operational reminder: “If enabled” means only successfully integrated digital modes produce logs; VARA/Winlink are Coming Soon. Single-receiver monitoring does not decode multiple bands concurrently; available modes depend on the actual integrated decoder and connected input source. Always distinguish receiver active, signal present, and successful decode.
