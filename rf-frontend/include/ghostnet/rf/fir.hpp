#pragma once

#include <complex>
#include <cstddef>
#include <vector>

namespace ghostnet::rf {

// Windowed-sinc low-pass FIR design (Hamming window). `taps` must be odd.
// Unity gain at DC.
std::vector<float> design_lowpass_fir(std::size_t taps, double cutoff_hz, double sample_rate_hz);

// Windowed FIR approximation of an ideal discrete Hilbert transformer
// (broadband 90-degree phase shifter). `taps` must be odd; even-lag
// coefficients, including the center, are zero.
std::vector<float> design_hilbert_fir(std::size_t taps);

// Real-coefficient FIR on a real stream, one sample in / one sample out.
class RealFir {
 public:
  explicit RealFir(std::vector<float> coefficients);

  float process(float sample);

  // (taps - 1) / 2: how far a parallel untouched branch must be delayed to
  // stay time-aligned with this filter's output.
  std::size_t group_delay() const { return (taps_.size() - 1) / 2; }

 private:
  std::vector<float> taps_;
  std::vector<float> history_;  // 2x length so the window is always contiguous
  std::size_t pos_ = 0;
};

// Real-coefficient FIR on a complex stream with integrated decimation: the
// dot product is only evaluated for samples that are kept, so the cost is
// taps * output_rate rather than taps * input_rate.
class DecimatingComplexFir {
 public:
  DecimatingComplexFir(std::vector<float> coefficients, std::size_t decimation);

  // Returns true and writes *out when this input completes an output sample.
  bool push(std::complex<float> sample, std::complex<float>* out);

  std::size_t decimation() const { return decimation_; }
  std::size_t taps() const { return taps_.size(); }

 private:
  std::vector<float> taps_;
  std::vector<float> re_;  // 2x length history, real parts
  std::vector<float> im_;  // 2x length history, imaginary parts
  std::size_t pos_ = 0;
  std::size_t decimation_;
  std::size_t phase_ = 0;
};

}  // namespace ghostnet::rf
