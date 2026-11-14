#include "ghostnet/rf/ssb_demodulator.hpp"

#include <stdexcept>

namespace ghostnet::rf {

SsbDemodulator::SsbDemodulator(SsbDemodulatorConfig config)
    : config_(config),
      lowpass_fir_(design_lowpass_fir(config.lowpass_taps, config.passband_high_hz, config.input_sample_rate_hz)),
      hilbert_fir_(design_hilbert_fir(config.hilbert_taps)),
      nco_(config.input_sample_rate_hz) {
  if (config_.input_sample_rate_hz <= 0.0 || config_.audio_sample_rate_hz <= 0.0) {
    throw std::invalid_argument("SsbDemodulator: sample rates must be positive");
  }
  const double ratio = config_.input_sample_rate_hz / config_.audio_sample_rate_hz;
  const auto rounded = static_cast<std::size_t>(ratio + 0.5);
  if (rounded == 0 || std::abs(ratio - static_cast<double>(rounded)) > 1e-6) {
    throw std::invalid_argument(
        "SsbDemodulator: input_sample_rate_hz must be an integer multiple of audio_sample_rate_hz "
        "(real capture rates must be chosen, or resampled upstream, to satisfy this)");
  }
  decimation_factor_ = rounded;
  nco_.set_frequency_hz(config_.tune_offset_hz);

  // The Hilbert transform now runs on the decimated (audio-rate) Q
  // branch, so its group delay is in decimated-sample units; the I branch
  // needs a matching delay line at that same rate.
  i_delay_line_.assign(hilbert_fir_.group_delay(), 0.0);
}

void SsbDemodulator::process(const std::int16_t* samples, std::size_t count, std::vector<float>* audio_out) {
  for (std::size_t n = 0; n < count; ++n) {
    const double x = static_cast<double>(samples[n]) / 32768.0;

    const std::complex<double> baseband = x * nco_.next();
    const std::complex<double> filtered = lowpass_fir_.process(baseband);

    if (++decimation_counter_ < decimation_factor_) {
      continue;
    }
    decimation_counter_ = 0;

    const double i_sample = filtered.real();
    const double q_sample = filtered.imag();

    // Phasing-method sideband selection: I - H{Q} passes the sideband
    // that landed at positive baseband frequencies after the mix above
    // (defined here as USB), and cancels the one that landed at negative
    // baseband frequencies (the image); I + H{Q} does the opposite. See
    // the derivation referenced from ssb_demodulator.hpp and the
    // synthetic check in tools/image_rejection_test.cpp.
    const double q_hilbert = hilbert_fir_.process(q_sample);
    i_delay_line_.push_back(i_sample);
    const double i_delayed = i_delay_line_.front();
    i_delay_line_.pop_front();

    const double audio = (config_.sideband == Sideband::kUsb) ? (i_delayed - q_hilbert) : (i_delayed + q_hilbert);
    audio_out->push_back(static_cast<float>(audio));
  }
}

}  // namespace ghostnet::rf
