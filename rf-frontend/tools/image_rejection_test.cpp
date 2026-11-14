// Synthetic-fixture validation of ghostnet::rf::SsbDemodulator's single
// most important receiver property: given a real (non-IQ) ADC-style input
// stream, it must be able to tell apart a signal on the wanted side of
// the tuned reference from an equal-strength signal the same distance on
// the *other* side (the classic SSB "image" problem). No RF, no USB, no
// device — see PHASE1_PLAN.md work stream 3. This is step 3 of
// PHASE0_FINDINGS.md's own recommended sequence: validate the DSP math
// against a generated fixture before it ever touches an antenna.
//
// Exits non-zero (and prints what failed) on any assertion failure so it
// can be wired into CTest/CI without a separate harness.

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

void expect_lt(double actual, double threshold, const char* what) {
  if (!(actual < threshold)) {
    std::fprintf(stderr, "FAIL: %s: expected < %.4f, got %.4f\n", what, threshold, actual);
    ++failures;
  }
}

// Runs one tone through the demodulator and returns the Goertzel
// magnitude at `measure_hz` in the decimated output, discarding the first
// `warmup` output samples so the FIR group-delay transient doesn't skew
// the measurement.
double run_single_tone(double rf_hz, double measure_hz, const SsbDemodulatorConfig& cfg, std::size_t warmup) {
  SsbDemodulator demod(cfg);
  const std::size_t input_count = static_cast<std::size_t>(cfg.input_sample_rate_hz * 2.0);  // 2 seconds
  const auto input = generate_tone(rf_hz, cfg.input_sample_rate_hz, input_count);

  std::vector<float> audio;
  demod.process_real_i16(input.data(), input.size(), &audio);

  if (audio.size() <= warmup) {
    std::fprintf(stderr, "FAIL: not enough output samples to discard warmup (%zu <= %zu)\n", audio.size(), warmup);
    ++failures;
    return 0.0;
  }
  std::vector<float> steady(audio.begin() + static_cast<long>(warmup), audio.end());
  return goertzel_magnitude(steady, measure_hz, cfg.audio_sample_rate_hz);
}

}  // namespace

int main() {
  SsbDemodulatorConfig cfg;
  cfg.input_sample_rate_hz = 48000.0;
  cfg.audio_sample_rate_hz = 12000.0;
  cfg.tune_offset_hz = 8000.0;  // stand-in "dial frequency" in this synthetic domain
  cfg.sideband = Sideband::kUsb;
  cfg.passband_high_hz = 2800.0;
  cfg.hilbert_taps = 129;
  cfg.lowpass_taps = 127;

  const double audio_tone_hz = 800.0;  // offset from the tuned reference
  const std::size_t warmup = 500;      // discard FIR settling time

  const double wanted_rf_hz = cfg.tune_offset_hz + audio_tone_hz;  // 8800 Hz: correct USB side
  const double image_rf_hz = cfg.tune_offset_hz - audio_tone_hz;   // 7200 Hz: the mirror/image side

  const double wanted_mag = run_single_tone(wanted_rf_hz, audio_tone_hz, cfg, warmup);
  const double image_mag = run_single_tone(image_rf_hz, audio_tone_hz, cfg, warmup);

  const double rejection_db = to_db(wanted_mag / std::max(image_mag, 1e-9));

  std::printf("wanted-side magnitude at %.0f Hz: %.6f\n", audio_tone_hz, wanted_mag);
  std::printf("image-side magnitude at %.0f Hz:  %.6f\n", audio_tone_hz, image_mag);
  std::printf("image rejection: %.1f dB\n", rejection_db);

  // The wanted tone must actually come through with reasonable gain...
  expect_gt(wanted_mag, 100.0, "wanted-side tone magnitude");
  // ...and the image must be rejected by a meaningful margin. 20 dB is a
  // conservative bar for a 129-tap Hamming-windowed Hilbert transformer;
  // it is a regression guard on this implementation, not a claim about
  // what real hardware will achieve.
  expect_gt(rejection_db, 20.0, "image rejection margin");

  // Sanity check in the other direction: configuring LSB should flip
  // which side is treated as wanted.
  SsbDemodulatorConfig lsb_cfg = cfg;
  lsb_cfg.sideband = Sideband::kLsb;
  const double lsb_wanted_mag = run_single_tone(image_rf_hz, audio_tone_hz, lsb_cfg, warmup);
  const double lsb_image_mag = run_single_tone(wanted_rf_hz, audio_tone_hz, lsb_cfg, warmup);
  const double lsb_rejection_db = to_db(lsb_wanted_mag / std::max(lsb_image_mag, 1e-9));
  std::printf("LSB wanted-side magnitude: %.6f, LSB image-side magnitude: %.6f, rejection: %.1f dB\n", lsb_wanted_mag,
              lsb_image_mag, lsb_rejection_db);
  expect_gt(lsb_wanted_mag, 100.0, "LSB wanted-side tone magnitude");
  expect_gt(lsb_rejection_db, 20.0, "LSB image rejection margin");

  if (failures > 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return EXIT_FAILURE;
  }
  std::printf("all checks passed\n");
  return EXIT_SUCCESS;
}
