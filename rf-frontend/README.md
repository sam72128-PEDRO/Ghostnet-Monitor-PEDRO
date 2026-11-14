# rf-frontend

First-party DSP for GhostNet Monitor's RTL-SDR receive path (PHASE1_PLAN.md,
work stream 3). Portable C++20, no Android/USB/RTL2832U dependency — it
takes a stream of real-valued samples and produces demodulated SSB audio
at a fixed output rate. Host-buildable and host-tested; that's deliberate,
matching PHASE0_FINDINGS.md's own recommended order: validate the DSP
against a generated fixture before it ever touches real RF.

## What's implemented and verified here

- `SsbDemodulator`: complex-mix down by a tuned offset, band-limit and
  decimate to a fixed audio rate, then separate the wanted sideband from
  its image with a phasing-method Hilbert combine
  (`audio = I - H{Q}` for USB, `I + H{Q}` for LSB).
- `tools/image_rejection_test.cpp`: generates a wanted-side tone and an
  equal-strength image-side tone (same offset, opposite side of the tuned
  reference) and asserts the image is suppressed by at least 20 dB.
  Current implementation measures ~83 dB with 129/127-tap filters. This
  caught a real bug during development — an earlier version applied the
  Hilbert transform before the mix instead of after, which measured 0 dB
  rejection (silently passing both sidebands). Keep this test; don't trust
  a refactor of this module that doesn't keep passing it.
- `tools/passband_test.cpp`: confirms the configured passband edge
  actually limits bandwidth (an out-of-band tone well past the edge, but
  still within the decimated output's Nyquist, is attenuated ~59 dB
  relative to an in-band tone).

Build and run:

```bash
cmake -S . -B build && cmake --build build -j
ctest --test-dir build --output-on-failure
```

## What this does NOT prove

This is a synthetic-signal, host-only test of the demodulation math. It
says nothing about:

- Whether RTL2832U Q-branch direct sampling actually produces a sample
  stream shaped the way this module assumes (real-valued, single ADC
  channel, known sample rate).
- What sample rates are actually sustainable over USB on the SM-T733.
- Real-world antialiasing, ADC nonlinearity, DC offset, or front-end gain
  behavior.
- Anything about the RTL2832U/R820T2 register sequence needed to put the
  dongle into that mode — that's `third_party/rtl-sdr-blog`'s job, and
  it isn't wired to this module yet.

See PHASE1_PLAN.md work stream 3 for what's still required before this
can be trusted with a live antenna.

## Integer decimation assumption

`SsbDemodulatorConfig::input_sample_rate_hz` must be an exact integer
multiple of `audio_sample_rate_hz` (constructor throws otherwise). Real
RTL-SDR capture rates won't necessarily satisfy that for a fixed 12 kHz
output — either pick a capture rate that does, or add a resampling stage
upstream. That choice is deliberately left as a TODO rather than silently
assumed away.
