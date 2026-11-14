#include "ghostnet/rf/nco.hpp"

#include <cmath>

namespace ghostnet::rf {

Nco::Nco(double sample_rate_hz) : sample_rate_hz_(sample_rate_hz) {}

void Nco::set_frequency_hz(double frequency_hz) {
  const double w = 2.0 * 3.14159265358979323846 * frequency_hz / sample_rate_hz_;
  step_ = {std::cos(w), -std::sin(w)};
}

std::complex<float> Nco::next() {
  const std::complex<float> out(static_cast<float>(phasor_.real()), static_cast<float>(phasor_.imag()));
  phasor_ *= step_;
  if (++since_normalize_ >= 1024) {
    since_normalize_ = 0;
    phasor_ /= std::abs(phasor_);
  }
  return out;
}

}  // namespace ghostnet::rf
