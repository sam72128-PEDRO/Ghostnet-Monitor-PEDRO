#include "ghostnet/rx/spectrum.hpp"

#include <cmath>
#include <stdexcept>

namespace ghostnet::rx {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

void fft_radix2(std::vector<std::complex<float>>& a) {
  const std::size_t n = a.size();
  if (n == 0 || (n & (n - 1)) != 0) throw std::invalid_argument("fft_radix2: size must be a power of two");
  for (std::size_t i = 1, j = 0; i < n; ++i) {
    std::size_t bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) std::swap(a[i], a[j]);
  }
  for (std::size_t len = 2; len <= n; len <<= 1) {
    const double ang = -2.0 * kPi / static_cast<double>(len);
    const std::complex<float> wlen(static_cast<float>(std::cos(ang)), static_cast<float>(std::sin(ang)));
    for (std::size_t i = 0; i < n; i += len) {
      std::complex<float> w(1.0f, 0.0f);
      for (std::size_t k = 0; k < len / 2; ++k) {
        const auto u = a[i + k];
        const auto v = a[i + k + len / 2] * w;
        a[i + k] = u + v;
        a[i + k + len / 2] = u - v;
        w *= wlen;
      }
    }
  }
}

SpectrumAnalyzer::SpectrumAnalyzer(double sample_rate_hz, std::size_t fft_size, std::size_t hop, double max_hz,
                                   double display_bin_hz)
    : rate_(sample_rate_hz),
      n_(fft_size),
      hop_(hop),
      display_bin_hz_(display_bin_hz),
      display_bins_(static_cast<std::size_t>(max_hz / display_bin_hz)),
      window_(fft_size),
      fft_(fft_size),
      row_(display_bins_) {
  for (std::size_t i = 0; i < n_; ++i) {
    window_[i] = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) / (n_ - 1)));
  }
  pending_.reserve(2 * n_);
}

void SpectrumAnalyzer::process(const float* samples, std::size_t count, const RowSink& sink) {
  pending_.insert(pending_.end(), samples, samples + count);
  while (pending_.size() >= n_) {
    compute_row(sink);
    pending_.erase(pending_.begin(), pending_.begin() + static_cast<long>(hop_));
  }
}

void SpectrumAnalyzer::compute_row(const RowSink& sink) {
  for (std::size_t i = 0; i < n_; ++i) fft_[i] = {pending_[i] * window_[i], 0.0f};
  fft_radix2(fft_);
  const double fft_bin_hz = rate_ / static_cast<double>(n_);
  // Normalise so a full-scale sine reads about 0 dB regardless of FFT size.
  const double scale = 4.0 / (static_cast<double>(n_) * static_cast<double>(n_));
  for (std::size_t b = 0; b < display_bins_; ++b) {
    const auto lo = static_cast<std::size_t>(std::floor(b * display_bin_hz_ / fft_bin_hz));
    auto hi = static_cast<std::size_t>(std::floor((b + 1) * display_bin_hz_ / fft_bin_hz));
    if (hi <= lo) hi = lo + 1;
    double p = 0.0;
    for (std::size_t k = lo; k < hi && k < n_ / 2; ++k) p = std::max(p, static_cast<double>(std::norm(fft_[k])));
    row_[b] = static_cast<float>(10.0 * std::log10(p * scale + 1e-20));
  }
  if (sink) sink(row_, static_cast<float>(display_bin_hz_));
}

}  // namespace ghostnet::rx
