#include "ghostnet/rf/fm_demodulator.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ghostnet::rf {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kMidRate = 48000.0;
constexpr double kNoiseCalibration = 0.12;

std::vector<float> highpass(std::size_t taps, double cutoff_hz, double rate) {
  // Spectral inversion of a low-pass: delta - lowpass.
  auto h = design_lowpass_fir(taps, cutoff_hz, rate);
  for (auto& v : h) v = -v;
  h[(taps - 1) / 2] += 1.0f;
  return h;
}
}  // namespace

FmDemodulator::FmDemodulator(FmDemodulatorConfig c)
    : config_(c),
      nco_(c.input_sample_rate_hz),
      stage1_(design_lowpass_fir(61, 12000.0, c.input_sample_rate_hz),
              static_cast<std::size_t>(std::lround(c.input_sample_rate_hz / kMidRate))),
      channel_(design_lowpass_fir(127, c.channel_half_width_hz - 1000.0, kMidRate), 1),
      audio_lpf_(design_lowpass_fir(95, c.audio_high_hz, kMidRate),
                 static_cast<std::size_t>(std::lround(kMidRate / c.audio_sample_rate_hz))),
      hf_noise_(highpass(63, 6000.0, kMidRate)) {
  if (std::abs(c.input_sample_rate_hz / kMidRate - std::lround(c.input_sample_rate_hz / kMidRate)) > 1e-9 ||
      std::abs(kMidRate / c.audio_sample_rate_hz - std::lround(kMidRate / c.audio_sample_rate_hz)) > 1e-9) {
    throw std::invalid_argument("FmDemodulator: input rate must be a multiple of 48 kHz, and 48 kHz of the audio rate");
  }
  nco_.set_frequency_hz(c.tune_offset_hz);
  if (c.deemphasis_us > 0) {
    deemph_alpha_ = static_cast<float>(1.0 - std::exp(-1.0 / (c.deemphasis_us * 1e-6 * c.audio_sample_rate_hz)));
  }
}

void FmDemodulator::push(std::complex<float> s, std::vector<float>* audio_out) {
  std::complex<float> wide;
  if (!stage1_.push(s * nco_.next(), &wide)) return;
  std::complex<float> mid;
  channel_.push(wide, &mid);

  // Polar discriminator: phase step between consecutive samples.
  const float dphi = std::arg(mid * std::conj(previous_));
  previous_ = mid;
  const float freq_hz = static_cast<float>(dphi * kMidRate / (2.0 * kPi));
  const float x = static_cast<float>(freq_hz / (2.0 * config_.nominal_deviation_hz));

  // Squelch metric from the noise band above the voice channel.
  const float n = hf_noise_.process(x);
  noise_power_ += 0.0005f * (n * n - noise_power_);
  if (++noise_count_ % 480 == 0) {
    // Normalised so band-limited noise with no carrier reads about 1 (measured
    // with tools/fm_test.cpp; a clean carrier reads below 0.1).
    const double full_scale = kMidRate / (2.0 * 2.0 * config_.nominal_deviation_hz);
    const double noise_reference = full_scale * full_scale / 3.0 * 0.75 * kNoiseCalibration;
    noise_ratio_ = static_cast<float>(std::min(1.0, std::sqrt(noise_power_ / noise_reference)));
  }

  std::complex<float> a;
  if (!audio_lpf_.push({x, 0.0f}, &a)) return;
  float y = a.real();
  if (deemph_alpha_ > 0.0f) {
    deemph_state_ += deemph_alpha_ * (y - deemph_state_);
    y = deemph_state_;
  }
  dc_state_ += 0.002f * (y - dc_state_);
  audio_out->push_back(y - dc_state_);
}

void FmDemodulator::process_iq_u8(const std::uint8_t* iq, std::size_t pairs, std::vector<float>* audio_out) {
  constexpr float k = 1.0f / 127.5f;
  for (std::size_t i = 0; i < pairs; ++i) {
    push({(static_cast<float>(iq[2 * i]) - 127.5f) * k, (static_cast<float>(iq[2 * i + 1]) - 127.5f) * k}, audio_out);
  }
}

void FmDemodulator::process_complex(const std::complex<float>* samples, std::size_t count,
                                    std::vector<float>* audio_out) {
  for (std::size_t i = 0; i < count; ++i) push(samples[i], audio_out);
}

}  // namespace ghostnet::rf
