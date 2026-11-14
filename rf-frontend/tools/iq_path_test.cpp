// Synthetic check of the path real hardware uses: RTL2832U-style
// interleaved u8 IQ at 240 kS/s, two-stage decimation to 12 kHz, USB
// selection. Covers sideband rejection, channel-filter rejection of a
// strong nearby signal, u8 quantisation with noise, the spectrum-invert
// switch, and throughput (the tablet has to keep up in real time).

#include <chrono>
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

#include "ghostnet/rf/ssb_demodulator.hpp"
#include "test_support.hpp"

using ghostnet::rf::Sideband;
using ghostnet::rf::SlowAgc;
using ghostnet::rf::SsbDemodulator;
using ghostnet::rf::SsbDemodulatorConfig;
using ghostnet::rf::test_support::goertzel_magnitude;
using ghostnet::rf::test_support::to_db;

namespace {

int failures = 0;

void expect(bool ok, const char* what, double value) {
  std::printf("%s %-46s %.2f\n", ok ? "ok  " : "FAIL", what, value);
  if (!ok) ++failures;
}

constexpr double kRate = 240000.0;
constexpr double kPi = 3.14159265358979323846;

// Complex tone at `hz` relative to the capture centre, quantised to the
// RTL2832U u8 wire format with additive Gaussian noise (in u8 counts).
std::vector<std::uint8_t> iq_tone(double hz, double amplitude, double noise_counts, std::size_t pairs,
                                  std::uint32_t seed = 1) {
  std::mt19937 rng(seed);
  std::normal_distribution<double> noise(0.0, noise_counts);
  std::vector<std::uint8_t> out(2 * pairs);
  for (std::size_t n = 0; n < pairs; ++n) {
    const double ph = 2.0 * kPi * hz * static_cast<double>(n) / kRate;
    const double i = 127.5 + 127.5 * amplitude * std::cos(ph) + noise(rng);
    const double q = 127.5 + 127.5 * amplitude * std::sin(ph) + noise(rng);
    out[2 * n] = static_cast<std::uint8_t>(std::clamp(std::lround(i), 0L, 255L));
    out[2 * n + 1] = static_cast<std::uint8_t>(std::clamp(std::lround(q), 0L, 255L));
  }
  return out;
}

double measure(const SsbDemodulatorConfig& cfg, const std::vector<std::uint8_t>& iq, double audio_hz) {
  SsbDemodulator demod(cfg);
  std::vector<float> audio;
  demod.process_iq_u8(iq.data(), iq.size() / 2, &audio);
  const std::size_t warmup = 1200;  // 100 ms at 12 kHz
  std::vector<float> steady(audio.begin() + static_cast<long>(warmup), audio.end());
  return goertzel_magnitude(steady, audio_hz, cfg.audio_sample_rate_hz);
}

}  // namespace

int main() {
  SsbDemodulatorConfig cfg;
  cfg.input_sample_rate_hz = kRate;
  cfg.audio_sample_rate_hz = 12000.0;
  cfg.tune_offset_hz = -25000.0;  // dongle tuned 25 kHz above the dial
  cfg.sideband = Sideband::kUsb;

  const std::size_t pairs = static_cast<std::size_t>(kRate * 2.0);
  const double audio_hz = 1500.0;

  // Clean-ish signals at a realistic small amplitude (-30 dBFS) so u8
  // quantisation actually matters.
  const double amp = 0.03;
  const double wanted = measure(cfg, iq_tone(cfg.tune_offset_hz + audio_hz, amp, 0.5, pairs), audio_hz);
  const double image = measure(cfg, iq_tone(cfg.tune_offset_hz - audio_hz, amp, 0.5, pairs), audio_hz);
  expect(wanted > 1.0, "USB tone passes (magnitude)", wanted);
  expect(to_db(wanted / image) > 40.0, "opposite sideband rejected (dB)", to_db(wanted / image));

  // A strong signal 8 kHz above the dial (outside the 2.8 kHz channel) must
  // not show up in the audio. Measured where it would fold to after the
  // final decimation (8000 - 12000 = -4000 -> 4000 Hz in a real output).
  const double strong = measure(cfg, iq_tone(cfg.tune_offset_hz + 8000.0, 0.5, 0.5, pairs), 4000.0);
  const double strong_ref = measure(cfg, iq_tone(cfg.tune_offset_hz + 1500.0, 0.5, 0.5, pairs), 1500.0);
  expect(to_db(strong_ref / std::max(strong, 1e-9)) > 40.0, "out-of-channel strong signal rejected (dB)",
         to_db(strong_ref / std::max(strong, 1e-9)));

  // DC artefact at the capture centre (25 kHz below the dial... i.e. at
  // -tune_offset relative to dial) must be far outside the passband.
  const double dc = measure(cfg, iq_tone(0.0, 0.2, 0.5, pairs), 1500.0);
  expect(to_db(strong_ref / std::max(dc, 1e-9)) > 60.0, "capture-centre DC rejected (dB)",
         to_db(strong_ref / std::max(dc, 1e-9)));

  // invert_spectrum: a mirrored stream should decode only with the switch on.
  SsbDemodulatorConfig inv = cfg;
  inv.invert_spectrum = true;
  const auto mirrored = iq_tone(-(cfg.tune_offset_hz + audio_hz), amp, 0.5, pairs);
  const double inv_on = measure(inv, mirrored, audio_hz);
  const double inv_off = measure(cfg, mirrored, audio_hz);
  expect(to_db(inv_on / std::max(inv_off, 1e-9)) > 40.0, "invert_spectrum recovers mirrored signal (dB)",
         to_db(inv_on / std::max(inv_off, 1e-9)));

  // AGC brings a weak demodulated tone to a usable int16 level.
  {
    SsbDemodulator demod(cfg);
    std::vector<float> audio;
    const auto iq = iq_tone(cfg.tune_offset_hz + audio_hz, 0.01, 0.5, pairs);
    demod.process_iq_u8(iq.data(), iq.size() / 2, &audio);
    SlowAgc agc(cfg.audio_sample_rate_hz);
    std::vector<std::int16_t> pcm;
    agc.process(audio.data(), audio.size(), &pcm);
    double ms = 0.0;
    for (std::size_t n = pcm.size() / 2; n < pcm.size(); ++n) ms += static_cast<double>(pcm[n]) * pcm[n];
    const double rms = std::sqrt(ms / static_cast<double>(pcm.size() / 2));
    expect(rms > 1500.0 && rms < 6000.0, "AGC output RMS in int16 counts", rms);
  }

  // Throughput: must comfortably exceed real time on the host; the tablet
  // is several times slower, so ask for a wide margin here.
  {
    SsbDemodulator demod(cfg);
    const auto iq = iq_tone(cfg.tune_offset_hz + audio_hz, amp, 0.5, static_cast<std::size_t>(kRate * 10.0));
    std::vector<float> audio;
    audio.reserve(130000);
    const auto t0 = std::chrono::steady_clock::now();
    demod.process_iq_u8(iq.data(), iq.size() / 2, &audio);
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const double realtime_factor = 10.0 / secs;
    expect(realtime_factor > 20.0, "throughput (x real time at 240 kS/s)", realtime_factor);
    std::printf("     latency_seconds()                              %.4f\n", demod.latency_seconds());
  }

  if (failures) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return EXIT_FAILURE;
  }
  std::printf("all checks passed\n");
  return EXIT_SUCCESS;
}
