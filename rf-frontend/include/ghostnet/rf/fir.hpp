#pragma once

#include <complex>
#include <cstddef>
#include <deque>
#include <vector>

namespace ghostnet::rf {

// Windowed-sinc low-pass FIR design (Hamming window). `taps` should be odd.
// Unity gain at DC.
std::vector<double> design_lowpass_fir(std::size_t taps, double cutoff_hz, double sample_rate_hz);

// Windowed FIR approximation of an ideal discrete Hilbert transformer
// (90-degree broadband phase shifter). `taps` must be odd; the center
// coefficient is zero. Used to build an analytic (single-sideband) signal
// from a real-valued ADC stream, which is required before mixing to
// baseband if the receiver must distinguish the two sides of the tuned
// frequency (true SSB selectivity) rather than just band-limiting a
// double-sideband product.
std::vector<double> design_hilbert_fir(std::size_t taps);

// Real-coefficient FIR applied to a complex sample stream (same
// coefficients applied independently to the real and imaginary parts).
// Keeps its own history so it can be fed one sample at a time across
// calls (streaming-correct, no block-boundary artifacts).
class ComplexFir {
 public:
  explicit ComplexFir(std::vector<double> coefficients);

  std::complex<double> process(std::complex<double> sample);

 private:
  std::vector<double> coefficients_;
  std::deque<std::complex<double>> history_;
};

// Real-coefficient FIR applied to a real sample stream, used to build the
// Hilbert ("imaginary") branch from raw ADC samples.
class RealFir {
 public:
  explicit RealFir(std::vector<double> coefficients);

  double process(double sample);

  // Group delay in samples: (taps - 1) / 2. The caller must delay the
  // untouched real branch by this many samples to keep it time-aligned
  // with this filter's output.
  std::size_t group_delay() const { return (coefficients_.size() - 1) / 2; }

 private:
  std::vector<double> coefficients_;
  std::deque<double> history_;
};

}  // namespace ghostnet::rf
