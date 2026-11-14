#pragma once

#include <complex>
#include <cstdint>

namespace ghostnet::rf {

// Numerically controlled oscillator for mixing a tuned offset down to 0 Hz.
// next() returns e^{-j*2*pi*f*n/fs}; multiply an input sample by it to shift
// `frequency_hz` to baseband. Uses a rotating phasor (one complex multiply
// per sample, no trig) and renormalizes periodically so amplitude error
// cannot accumulate over long sessions.
class Nco {
 public:
  explicit Nco(double sample_rate_hz);

  void set_frequency_hz(double frequency_hz);

  std::complex<float> next();

 private:
  double sample_rate_hz_;
  std::complex<double> phasor_{1.0, 0.0};
  std::complex<double> step_{1.0, 0.0};
  std::uint32_t since_normalize_ = 0;
};

}  // namespace ghostnet::rf
