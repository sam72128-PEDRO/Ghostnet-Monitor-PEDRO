#pragma once

#include <complex>

namespace ghostnet::rf {

// Numerically controlled oscillator used to mix a tuned RF offset down to
// (or up from) baseband. next() returns one complex local-oscillator
// sample per call at e^{-j*phase}, matching the down-mixing convention
// used by SsbDemodulator (multiply the input sample by this value to
// shift `frequency_hz` down to 0 Hz).
class Nco {
 public:
  explicit Nco(double sample_rate_hz);

  void set_frequency_hz(double frequency_hz);

  std::complex<double> next();

 private:
  double sample_rate_hz_;
  double phase_ = 0.0;
  double phase_increment_ = 0.0;
};

}  // namespace ghostnet::rf
