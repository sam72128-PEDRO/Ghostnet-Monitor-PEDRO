#include "ghostnet/rf/ssb_demodulator.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ghostnet::rf {

namespace {

std::size_t decimation_ratio(const SsbDemodulatorConfig& c) {
  if (c.input_sample_rate_hz <= 0.0 || c.audio_sample_rate_hz <= 0.0) {
    throw std::invalid_argument("SsbDemodulator: sample rates must be positive");
  }
  const double ratio = c.input_sample_rate_hz / c.audio_sample_rate_hz;
  const auto rounded = static_cast<std::size_t>(std::lround(ratio));
  if (rounded == 0 || std::abs(ratio - static_cast<double>(rounded)) > 1e-6) {
    throw std::invalid_argument(
        "SsbDemodulator: input_sample_rate_hz must be an integer multiple of audio_sample_rate_hz");
  }
  return rounded;
}

bool two_stage(std::size_t ratio) { return ratio > 4 && ratio % 4 == 0; }

// First stage only has to stop energy that would alias into the final
// +/- audio_rate/2 band after decimating to 4x the audio rate, so a wide
// transition is fine. Tap count scales with the decimation factor to keep
// the transition width roughly constant in Hz.
std::unique_ptr<DecimatingComplexFir> make_stage1(const SsbDemodulatorConfig& c, std::size_t ratio) {
  if (!two_stage(ratio)) return nullptr;
  const std::size_t factor = ratio / 4;
  const std::size_t taps = 12 * factor + 1;
  const double mid_rate = c.audio_sample_rate_hz * 4.0;
  return std::make_unique<DecimatingComplexFir>(design_lowpass_fir(taps, mid_rate * 0.25, c.input_sample_rate_hz),
                                                factor);
}

DecimatingComplexFir make_stage2(const SsbDemodulatorConfig& c, std::size_t ratio) {
  const double rate = two_stage(ratio) ? c.audio_sample_rate_hz * 4.0 : c.input_sample_rate_hz;
  const std::size_t factor = two_stage(ratio) ? 4 : ratio;
  return DecimatingComplexFir(design_lowpass_fir(c.lowpass_taps, c.passband_high_hz, rate), factor);
}

}  // namespace

SsbDemodulator::SsbDemodulator(SsbDemodulatorConfig config)
    : config_(config),
      nco_(config.input_sample_rate_hz),
      stage1_(make_stage1(config, decimation_ratio(config))),
      stage2_(make_stage2(config, decimation_ratio(config))),
      hilbert_(design_hilbert_fir(config.hilbert_taps)),
      i_delay_(hilbert_.group_delay(), 0.0f) {
  nco_.set_frequency_hz(config_.tune_offset_hz);
}

double SsbDemodulator::latency_seconds() const {
  double latency = 0.0;
  double rate = config_.input_sample_rate_hz;
  if (stage1_) {
    latency += (static_cast<double>(stage1_->taps()) - 1.0) / 2.0 / rate;
    rate /= static_cast<double>(stage1_->decimation());
  }
  latency += (static_cast<double>(stage2_.taps()) - 1.0) / 2.0 / rate;
  latency += static_cast<double>(hilbert_.group_delay()) / config_.audio_sample_rate_hz;
  return latency;
}

void SsbDemodulator::push(std::complex<float> sample, std::vector<float>* audio_out) {
  if (config_.invert_spectrum) sample = std::conj(sample);
  std::complex<float> v = sample * nco_.next();

  if (stage1_) {
    std::complex<float> mid;
    if (!stage1_->push(v, &mid)) return;
    v = mid;
  }
  std::complex<float> base;
  if (!stage2_.push(v, &base)) return;

  const float q_hilbert = hilbert_.process(base.imag());
  float i_delayed = base.real();
  if (!i_delay_.empty()) {
    i_delayed = i_delay_[i_delay_pos_];
    i_delay_[i_delay_pos_] = base.real();
    i_delay_pos_ = (i_delay_pos_ + 1) % i_delay_.size();
  }
  audio_out->push_back(config_.sideband == Sideband::kUsb ? i_delayed - q_hilbert : i_delayed + q_hilbert);
}

void SsbDemodulator::process_iq_u8(const std::uint8_t* iq, std::size_t pairs, std::vector<float>* audio_out) {
  constexpr float kScale = 1.0f / 127.5f;
  for (std::size_t n = 0; n < pairs; ++n) {
    push({(static_cast<float>(iq[2 * n]) - 127.5f) * kScale, (static_cast<float>(iq[2 * n + 1]) - 127.5f) * kScale},
         audio_out);
  }
}

void SsbDemodulator::process_complex(const std::complex<float>* samples, std::size_t count,
                                     std::vector<float>* audio_out) {
  for (std::size_t n = 0; n < count; ++n) push(samples[n], audio_out);
}

void SsbDemodulator::process_real_i16(const std::int16_t* samples, std::size_t count, std::vector<float>* audio_out) {
  for (std::size_t n = 0; n < count; ++n) push({static_cast<float>(samples[n]) / 32768.0f, 0.0f}, audio_out);
}

SlowAgc::SlowAgc(double sample_rate_hz, double target_rms, double time_constant_s, double max_gain)
    : alpha_(1.0 - std::exp(-1.0 / (time_constant_s * sample_rate_hz))), target_rms_(target_rms), max_gain_(max_gain) {}

void SlowAgc::process(const float* in, std::size_t count, std::vector<std::int16_t>* out) {
  for (std::size_t n = 0; n < count; ++n) {
    const double x = in[n];
    if (!primed_) {
      mean_square_ = x * x;
      primed_ = true;
    } else {
      mean_square_ += alpha_ * (x * x - mean_square_);
    }
    const double rms = std::sqrt(mean_square_);
    gain_ = rms > 0.0 ? std::min(max_gain_, target_rms_ / rms) : max_gain_;
    const double y = std::clamp(x * gain_, -32767.0, 32767.0);
    out->push_back(static_cast<std::int16_t>(std::lround(y)));
  }
}

double SlowAgc::input_level_dbfs() const {
  // A full-scale (amplitude 1.0) sine has mean square 0.5.
  return 10.0 * std::log10(std::max(mean_square_ / 0.5, 1e-20));
}

}  // namespace ghostnet::rf
