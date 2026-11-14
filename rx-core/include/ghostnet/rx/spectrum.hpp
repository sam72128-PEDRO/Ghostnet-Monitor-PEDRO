#pragma once

#include <complex>
#include <cstddef>
#include <functional>
#include <vector>

namespace ghostnet::rx {

// Waterfall rows from the demodulated 12 kHz audio: Hann-windowed FFT with
// overlap, reduced to fixed-width display bins (peak of the FFT bins in each) from 0 Hz to max_hz, in
// dB. At the defaults (2048-point FFT, 1024 hop, 10 Hz bins to 3 kHz) a row
// arrives about every 85 ms with 300 bins; a JS8 signal (8 tones, 6.25 Hz
// apart in Normal mode) shows as a ~50 Hz wide trace.
class SpectrumAnalyzer {
 public:
  using RowSink = std::function<void(const std::vector<float>& db_bins, float bin_hz)>;

  SpectrumAnalyzer(double sample_rate_hz, std::size_t fft_size = 2048, std::size_t hop = 1024,
                   double max_hz = 3000.0, double display_bin_hz = 10.0);

  void process(const float* samples, std::size_t count, const RowSink& sink);

  std::size_t bins() const { return display_bins_; }

 private:
  void compute_row(const RowSink& sink);

  double rate_;
  std::size_t n_;
  std::size_t hop_;
  double display_bin_hz_;
  std::size_t display_bins_;
  std::vector<float> window_;
  std::vector<float> pending_;
  std::vector<std::complex<float>> fft_;
  std::vector<float> row_;
};

// In-place iterative radix-2 FFT; size must be a power of two.
void fft_radix2(std::vector<std::complex<float>>& data);

}  // namespace ghostnet::rx
