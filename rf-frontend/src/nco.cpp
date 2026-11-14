#include "ghostnet/rf/nco.hpp"

#include <cmath>

namespace ghostnet::rf {

Nco::Nco(double sample_rate_hz) : sample_rate_hz_(sample_rate_hz) {}

void Nco::set_frequency_hz(double frequency_hz) {
  phase_increment_ = 2.0 * M_PI * frequency_hz / sample_rate_hz_;
}

std::complex<double> Nco::next() {
  const std::complex<double> lo(std::cos(phase_), -std::sin(phase_));
  phase_ += phase_increment_;
  if (phase_ > 2.0 * M_PI || phase_ < -2.0 * M_PI) {
    phase_ -= 2.0 * M_PI * std::floor(phase_ / (2.0 * M_PI));
  }
  return lo;
}

}  // namespace ghostnet::rf
