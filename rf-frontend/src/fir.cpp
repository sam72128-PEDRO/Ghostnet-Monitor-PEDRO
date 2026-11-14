#include "ghostnet/rf/fir.hpp"

#include <cmath>
#include <stdexcept>

namespace ghostnet::rf {

namespace {

double hamming(std::size_t n, std::size_t taps) {
  return 0.54 - 0.46 * std::cos(2.0 * M_PI * static_cast<double>(n) / static_cast<double>(taps - 1));
}

double sinc(double x) {
  if (std::abs(x) < 1e-12) return 1.0;
  return std::sin(M_PI * x) / (M_PI * x);
}

}  // namespace

std::vector<double> design_lowpass_fir(std::size_t taps, double cutoff_hz, double sample_rate_hz) {
  if (taps % 2 == 0 || taps < 3) {
    throw std::invalid_argument("design_lowpass_fir: taps must be odd and >= 3");
  }
  const double fc = cutoff_hz / sample_rate_hz;  // normalized cutoff, cycles/sample
  const std::size_t center = (taps - 1) / 2;
  std::vector<double> h(taps);
  double sum = 0.0;
  for (std::size_t n = 0; n < taps; ++n) {
    const double m = static_cast<double>(n) - static_cast<double>(center);
    h[n] = 2.0 * fc * sinc(2.0 * fc * m) * hamming(n, taps);
    sum += h[n];
  }
  if (sum != 0.0) {
    for (double& v : h) v /= sum;  // unity gain at DC
  }
  return h;
}

std::vector<double> design_hilbert_fir(std::size_t taps) {
  if (taps % 2 == 0 || taps < 3) {
    throw std::invalid_argument("design_hilbert_fir: taps must be odd and >= 3");
  }
  const std::size_t center = (taps - 1) / 2;
  std::vector<double> h(taps, 0.0);
  for (std::size_t n = 0; n < taps; ++n) {
    const long m = static_cast<long>(n) - static_cast<long>(center);
    if (m == 0 || m % 2 == 0) {
      h[n] = 0.0;  // ideal Hilbert response is zero for even lags
    } else {
      h[n] = (2.0 / (M_PI * static_cast<double>(m))) * hamming(n, taps);
    }
  }
  return h;
}

ComplexFir::ComplexFir(std::vector<double> coefficients) : coefficients_(std::move(coefficients)) {
  history_.assign(coefficients_.size(), std::complex<double>(0.0, 0.0));
}

std::complex<double> ComplexFir::process(std::complex<double> sample) {
  history_.push_front(sample);
  history_.pop_back();
  std::complex<double> acc(0.0, 0.0);
  std::size_t i = 0;
  for (double c : coefficients_) {
    acc += c * history_[i];
    ++i;
  }
  return acc;
}

RealFir::RealFir(std::vector<double> coefficients) : coefficients_(std::move(coefficients)) {
  history_.assign(coefficients_.size(), 0.0);
}

double RealFir::process(double sample) {
  history_.push_front(sample);
  history_.pop_back();
  double acc = 0.0;
  std::size_t i = 0;
  for (double c : coefficients_) {
    acc += c * history_[i];
    ++i;
  }
  return acc;
}

}  // namespace ghostnet::rf
