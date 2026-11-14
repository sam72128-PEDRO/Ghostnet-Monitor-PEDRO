# rf-frontend

First-party DSP for GhostNet Monitor's RTL-SDR receive path. Portable
C++20, no Android/USB dependency, host-buildable and host-tested.

## Signal path

RTL2832U output is interleaved unsigned 8-bit I/Q. That holds in Q-branch
direct sampling too: the ADC samples the HF input as a real signal, but the
chip's internal digital downconverter mixes it to complex baseband before
it reaches USB. (An earlier version of this README said direct sampling
delivers real samples. That was wrong.)

```
u8 IQ @ 240 kS/s
  -> optional conjugate (invert_spectrum)
  -> complex mix by tune_offset_hz (dial - capture centre)
  -> stage 1: decimating low-pass, /5  -> 48 kS/s
  -> stage 2: decimating low-pass, /4  -> 12 kS/s, 2.8 kHz channel
  -> phasing-method sideband select: USB = I - H{Q}, LSB = I + H{Q}
  -> SlowAgc -> int16 PCM @ 12 kHz (js8core's input contract)
```

The decimating filters compute only the samples they keep. The dongle is
tuned 25 kHz away from the dial so its DC/LO artefact lands outside the
channel.

## Tests (synthetic fixtures, run with ctest)

| Test | Checks | Current result |
|---|---|---|
| `iq_path_test` | Real hardware path: u8 IQ at 240 kS/s with noise. Opposite-sideband rejection, strong out-of-channel signal, capture-centre DC, invert switch, AGC level, throughput | 63 dB / 139 dB / 97 dB / 92 dB / ~4200 RMS / ~100x real time on host |
| `image_rejection_test` | Sideband selection with a real-valued input, USB and LSB | ~83 dB |
| `passband_test` | Channel filter edge | ~59 dB |

`image_rejection_test` caught a real bug during development. The Hilbert
transform had been applied before the mix, which measured 0 dB rejection.
Don't merge a change to this module that stops it passing.

```bash
cmake -S . -B build && cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Not proven here

- Whether Q-branch direct sampling on a real V3 delivers an inverted
  spectrum. If JS8 never decodes on a known-good signal, try
  `invert_spectrum` first.
- Real-world front-end behaviour: overload, ADC nonlinearity, and noise
  from the tablet or USB hub.
- Sustained throughput on the SM-T733 (host runs about 100x real time;
  the tablet should still have a wide margin, but that hasn't been measured).
