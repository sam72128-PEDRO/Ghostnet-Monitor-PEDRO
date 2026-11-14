// Synthetic-fixture check that SsbDemodulator's passband edge
// (passband_high_hz) actually limits bandwidth: a tone well inside the
// configured passband should pass with reasonable gain, and a tone well
// past the edge (but still within the decimated output's Nyquist, so this
// is testing the filter's skirt, not aliasing) should be substantially
// attenuated. See image_rejection_test.cpp for the sideband-selectivity
// check; this test only covers plain low-pass behavior.

#include <cstdio>
#include <cstdlib>
#include <vector>

#include "ghostnet/rf/ssb_demodulator.hpp"
#include "test_support.hpp"

using ghostnet::rf::Sideband;
using ghostnet::rf::SsbDemodulator;
using ghostnet::rf::SsbDemodulatorConfig;
using ghostnet::rf::test_support::generate_tone;
using ghostnet::rf::test_support::goertzel_magnitude;
using ghostnet::rf::test_support::to_db;

namespace {
int failures = 0;

void expect_gt(double actual, double threshold, const char* what) {
  if (!(actual > threshold)) {
    std::fprintf(stderr, "FAIL: %s: expected > %.4f, got %.4f\n", what, threshold, actual);
    ++failures;
  }
}
}  // namespace

int main() {
  SsbDemodulatorConfig cfg;
  cfg.input_sample_rate_hz = 48000.0;
  cfg.audio_sample_rate_hz = 12000.0;
  cfg.tune_offset_hz = 8000.0;
  cfg.sideband = Sideband::kUsb;
  cfg.passband_high_hz = 2800.0;
  cfg.hilbert_taps = 129;
  cfg.lowpass_taps = 127;

  const std::size_t warmup = 500;
  const std::size_t input_count = static_cast<std::size_t>(cfg.input_sample_rate_hz * 2.0);

  const double in_band_audio_hz = 1000.0;
  const double out_of_band_audio_hz = 5000.0;  // within post-decimation Nyquist (6000 Hz), past the 2800 Hz edge

  SsbDemodulator in_band_demod(cfg);
  auto in_band_input = generate_tone(cfg.tune_offset_hz + in_band_audio_hz, cfg.input_sample_rate_hz, input_count);
  std::vector<float> in_band_audio;
  in_band_demod.process(in_band_input.data(), in_band_input.size(), &in_band_audio);
  std::vector<float> in_band_steady(in_band_audio.begin() + static_cast<long>(warmup), in_band_audio.end());
  const double in_band_mag = goertzel_magnitude(in_band_steady, in_band_audio_hz, cfg.audio_sample_rate_hz);

  SsbDemodulator oob_demod(cfg);
  auto oob_input = generate_tone(cfg.tune_offset_hz + out_of_band_audio_hz, cfg.input_sample_rate_hz, input_count);
  std::vector<float> oob_audio;
  oob_demod.process(oob_input.data(), oob_input.size(), &oob_audio);
  std::vector<float> oob_steady(oob_audio.begin() + static_cast<long>(warmup), oob_audio.end());
  const double oob_mag_at_oob_freq = goertzel_magnitude(oob_steady, out_of_band_audio_hz, cfg.audio_sample_rate_hz);

  const double attenuation_db = to_db(in_band_mag / std::max(oob_mag_at_oob_freq, 1e-9));

  std::printf("in-band (%.0f Hz) magnitude: %.6f\n", in_band_audio_hz, in_band_mag);
  std::printf("out-of-band (%.0f Hz) magnitude: %.6f\n", out_of_band_audio_hz, oob_mag_at_oob_freq);
  std::printf("skirt attenuation: %.1f dB\n", attenuation_db);

  expect_gt(in_band_mag, 100.0, "in-band tone magnitude");
  expect_gt(attenuation_db, 20.0, "out-of-band skirt attenuation");

  if (failures > 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return EXIT_FAILURE;
  }
  std::printf("all checks passed\n");
  return EXIT_SUCCESS;
}
