// Narrowband FM voice demodulation on RTL-SDR-format IQ (240 kS/s u8):
// a 1 kHz test tone at 2.5 kHz deviation comes out clean, de-emphasis
// rolls off highs as specified, and the squelch metric separates a carrier
// from noise.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

#include "ghostnet/rf/fm_demodulator.hpp"
#include "test_support.hpp"

using ghostnet::rf::FmDemodulator;
using ghostnet::rf::FmDemodulatorConfig;
using ghostnet::rf::test_support::goertzel_magnitude;
using ghostnet::rf::test_support::to_db;

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kFs = 240000.0;
int failures = 0;

void expect(bool ok, const char* what, double v) {
  std::printf("%s %-52s %.2f\n", ok ? "ok  " : "FAIL", what, v);
  if (!ok) ++failures;
}

// FM carrier at `offset_hz` from capture centre, modulated by a tone.
std::vector<std::uint8_t> fm_iq(double offset_hz, double tone_hz, double deviation_hz, double amp_counts,
                                double noise_counts, double seconds, bool carrier = true) {
  std::mt19937 rng(7);
  std::normal_distribution<double> noise(0.0, noise_counts);
  const auto n = static_cast<std::size_t>(seconds * kFs);
  std::vector<std::uint8_t> out(2 * n);
  double phase = 0.0;
  for (std::size_t i = 0; i < n; ++i) {
    const double t = static_cast<double>(i) / kFs;
    phase += 2.0 * kPi * (offset_hz + deviation_hz * std::sin(2.0 * kPi * tone_hz * t)) / kFs;
    phase = std::remainder(phase, 2.0 * kPi);
    const double a = carrier ? amp_counts : 0.0;
    out[2 * i] = static_cast<std::uint8_t>(std::clamp(std::lround(127.5 + a * std::cos(phase) + noise(rng)), 0L, 255L));
    out[2 * i + 1] = static_cast<std::uint8_t>(std::clamp(std::lround(127.5 + a * std::sin(phase) + noise(rng)), 0L, 255L));
  }
  return out;
}

struct Result {
  double tone_mag;
  double rms;
  float noise_ratio;
};

Result run(const std::vector<std::uint8_t>& iq, double tone_hz, double deemph_us = 750.0) {
  FmDemodulatorConfig c;
  c.tune_offset_hz = -25000.0;
  c.deemphasis_us = deemph_us;
  FmDemodulator fm(c);
  std::vector<float> audio;
  fm.process_iq_u8(iq.data(), iq.size() / 2, &audio);
  std::vector<float> steady(audio.begin() + 3000, audio.end());
  double ss = 0.0;
  for (float v : steady) ss += static_cast<double>(v) * v;
  return {goertzel_magnitude(steady, tone_hz, 12000.0) * 2.0 / steady.size(), std::sqrt(ss / steady.size()), fm.noise_ratio()};
}
}  // namespace

int main() {
  // Clean voice-like tone, moderate signal with noise.
  const auto clean = run(fm_iq(-25000.0, 1000.0, 2500.0, 20.0, 3.0, 2.0), 1000.0, 0.0);
  const double tone_rms = clean.tone_mag / std::sqrt(2.0);
  const double residual = std::sqrt(std::max(clean.rms * clean.rms - tone_rms * tone_rms, 1e-12));
  expect(clean.tone_mag > 0.3 && clean.tone_mag < 0.8, "1 kHz tone amplitude at nominal deviation", clean.tone_mag);
  expect(to_db(tone_rms / residual) > 20.0, "tone-to-residual ratio, no de-emphasis (dB)", to_db(tone_rms / residual));

  // De-emphasis: 2.5 kHz tone attenuated much more than 300 Hz.
  const auto low = run(fm_iq(-25000.0, 300.0, 2500.0, 20.0, 1.0, 2.0), 300.0);
  const auto high = run(fm_iq(-25000.0, 2500.0, 2500.0, 20.0, 1.0, 2.0), 2500.0);
  expect(to_db(low.tone_mag / high.tone_mag) > 10.0, "de-emphasis 300 Hz vs 2.5 kHz (dB)", to_db(low.tone_mag / high.tone_mag));

  // Squelch metric: noise only vs carrier.
  const auto noise_only = run(fm_iq(-25000.0, 1000.0, 2500.0, 0.0, 6.0, 2.0, false), 1000.0);
  const auto carrier = run(fm_iq(-25000.0, 1000.0, 2500.0, 20.0, 6.0, 2.0), 1000.0);
  std::printf("     squelch metric: noise only %.3f, with carrier %.3f\n", noise_only.noise_ratio, carrier.noise_ratio);
  expect(noise_only.noise_ratio > 0.6, "squelch metric high with no carrier", noise_only.noise_ratio);
  expect(carrier.noise_ratio < 0.3, "squelch metric low with a carrier", carrier.noise_ratio);

  // Adjacent channel 12.5 kHz away must not come through.
  const auto adjacent = run(fm_iq(-25000.0 + 12500.0, 1000.0, 2500.0, 20.0, 1.0, 2.0), 1000.0, 0.0);
  expect(to_db(clean.tone_mag / std::max(adjacent.tone_mag, 1e-9)) > 30.0, "adjacent channel (12.5 kHz) rejection (dB)",
         to_db(clean.tone_mag / std::max(adjacent.tone_mag, 1e-9)));

  if (failures) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return EXIT_FAILURE;
  }
  std::printf("all checks passed\n");
  return EXIT_SUCCESS;
}
