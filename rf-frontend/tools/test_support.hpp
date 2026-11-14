#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ghostnet::rf::test_support {

// Generates `count` real samples of a cosine at frequency_hz, sampled at
// sample_rate_hz, scaled to amplitude * int16 full scale.
inline std::vector<std::int16_t> generate_tone(double frequency_hz, double sample_rate_hz, std::size_t count,
                                                double amplitude = 0.5) {
  std::vector<std::int16_t> out(count);
  for (std::size_t n = 0; n < count; ++n) {
    const double t = static_cast<double>(n) / sample_rate_hz;
    const double v = amplitude * std::cos(2.0 * M_PI * frequency_hz * t);
    out[n] = static_cast<std::int16_t>(v * 32767.0);
  }
  return out;
}

// Single-bin Goertzel magnitude of `target_hz` within `samples`, sampled
// at sample_rate_hz. Equivalent to |DFT bin| for that exact frequency,
// without computing the full transform.
inline double goertzel_magnitude(const std::vector<float>& samples, double target_hz, double sample_rate_hz) {
  const double omega = 2.0 * M_PI * target_hz / sample_rate_hz;
  const double coeff = 2.0 * std::cos(omega);
  double s_prev = 0.0;
  double s_prev2 = 0.0;
  for (float x : samples) {
    const double s = static_cast<double>(x) + coeff * s_prev - s_prev2;
    s_prev2 = s_prev;
    s_prev = s;
  }
  const double real = s_prev - s_prev2 * std::cos(omega);
  const double imag = s_prev2 * std::sin(omega);
  return std::sqrt(real * real + imag * imag);
}

inline double to_db(double ratio) { return 20.0 * std::log10(std::max(ratio, 1e-12)); }

}  // namespace ghostnet::rf::test_support
