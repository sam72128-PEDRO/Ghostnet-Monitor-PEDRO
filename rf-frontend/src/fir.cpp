#include "ghostnet/rf/fir.hpp"

#include <cmath>
#include <stdexcept>

namespace ghostnet::rf {

namespace {

constexpr double kPi = 3.14159265358979323846;

double hamming(std::size_t n, std::size_t taps) {
  return 0.54 - 0.46 * std::cos(2.0 * kPi * static_cast<double>(n) / static_cast<double>(taps - 1));
}

double sinc(double x) {
  if (std::abs(x) < 1e-12) return 1.0;
  return std::sin(kPi * x) / (kPi * x);
}

void require_odd(std::size_t taps, const char* who) {
  if (taps % 2 == 0 || taps < 3) throw std::invalid_argument(std::string(who) + ": taps must be odd and >= 3");
}

// Dot product of `taps` against a contiguous window starting at `window`.
// Written as a plain loop so compilers auto-vectorize it (NEON on arm64).
inline float dot(const float* taps, const float* window, std::size_t n) {
  float acc = 0.0f;
  for (std::size_t i = 0; i < n; ++i) acc += taps[i] * window[i];
  return acc;
}

}  // namespace

std::vector<float> design_lowpass_fir(std::size_t taps, double cutoff_hz, double sample_rate_hz) {
  require_odd(taps, "design_lowpass_fir");
  const double fc = cutoff_hz / sample_rate_hz;
  const std::size_t center = (taps - 1) / 2;
  std::vector<double> h(taps);
  double sum = 0.0;
  for (std::size_t n = 0; n < taps; ++n) {
    const double m = static_cast<double>(n) - static_cast<double>(center);
    h[n] = 2.0 * fc * sinc(2.0 * fc * m) * hamming(n, taps);
    sum += h[n];
  }
  std::vector<float> out(taps);
  for (std::size_t n = 0; n < taps; ++n) out[n] = static_cast<float>(h[n] / sum);
  return out;
}

std::vector<float> design_hilbert_fir(std::size_t taps) {
  require_odd(taps, "design_hilbert_fir");
  const long center = static_cast<long>((taps - 1) / 2);
  std::vector<float> h(taps, 0.0f);
  for (std::size_t n = 0; n < taps; ++n) {
    const long m = static_cast<long>(n) - center;
    if (m % 2 != 0) h[n] = static_cast<float>((2.0 / (kPi * static_cast<double>(m))) * hamming(n, taps));
  }
  return h;
}

// Both filters keep a history buffer of 2*N and write every sample twice
// (at pos and pos+N), so the most recent N samples are always available as
// one contiguous run ending at pos+N-1, oldest first. Taps are stored
// reversed to match that oldest-first ordering.

RealFir::RealFir(std::vector<float> coefficients)
    : taps_(coefficients.rbegin(), coefficients.rend()), history_(2 * coefficients.size(), 0.0f) {}

float RealFir::process(float sample) {
  const std::size_t n = taps_.size();
  history_[pos_] = sample;
  history_[pos_ + n] = sample;
  pos_ = (pos_ + 1) % n;
  return dot(taps_.data(), history_.data() + pos_, n);
}

DecimatingComplexFir::DecimatingComplexFir(std::vector<float> coefficients, std::size_t decimation)
    : taps_(coefficients.rbegin(), coefficients.rend()),
      re_(2 * coefficients.size(), 0.0f),
      im_(2 * coefficients.size(), 0.0f),
      decimation_(decimation) {
  if (decimation_ == 0) throw std::invalid_argument("DecimatingComplexFir: decimation must be >= 1");
}

bool DecimatingComplexFir::push(std::complex<float> sample, std::complex<float>* out) {
  const std::size_t n = taps_.size();
  re_[pos_] = sample.real();
  re_[pos_ + n] = sample.real();
  im_[pos_] = sample.imag();
  im_[pos_ + n] = sample.imag();
  pos_ = (pos_ + 1) % n;
  if (++phase_ < decimation_) return false;
  phase_ = 0;
  *out = {dot(taps_.data(), re_.data() + pos_, n), dot(taps_.data(), im_.data() + pos_, n)};
  return true;
}

}  // namespace ghostnet::rf
