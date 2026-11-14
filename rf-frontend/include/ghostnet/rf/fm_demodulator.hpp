#pragma once

#include <complex>
#include <cstdint>
#include <memory>
#include <vector>

#include "ghostnet/rf/fir.hpp"
#include "ghostnet/rf/nco.hpp"

namespace ghostnet::rf {

struct FmDemodulatorConfig {
  double input_sample_rate_hz = 240000.0;  // must be 20x audio_sample_rate_hz
  double audio_sample_rate_hz = 12000.0;
  // Channel centre relative to the capture centre (channel_hz - centre_hz).
  double tune_offset_hz = 0.0;
  // Narrowband FM (12.5 kHz amateur channels): +/-2.5-5 kHz deviation.
  double channel_half_width_hz = 7000.0;
  double nominal_deviation_hz = 2500.0;  // maps to audio amplitude ~0.5
  // De-emphasis time constant. Land-mobile / amateur NBFM transmitters use
  // 6 dB/octave pre-emphasis equivalent to ~750 us. 0 disables.
  double deemphasis_us = 750.0;
  double audio_high_hz = 3000.0;
};

// Narrowband FM voice receiver: complex mix to the channel, decimate to
// 48 kHz, polar discriminator, audio low-pass and decimation to 12 kHz,
// de-emphasis and DC removal. Also reports a noise-based squelch metric:
// with no carrier the discriminator output is dominated by noise above the
// voice band; a carrier quietens it.
class FmDemodulator {
 public:
  explicit FmDemodulator(FmDemodulatorConfig config);

  void process_iq_u8(const std::uint8_t* iq, std::size_t pairs, std::vector<float>* audio_out);
  void process_complex(const std::complex<float>* samples, std::size_t count, std::vector<float>* audio_out);

  // Smoothed ratio of out-of-voice-band discriminator noise to its no-signal
  // level, 0 (clean carrier) .. ~1 (no carrier).
  float noise_ratio() const { return noise_ratio_; }

 private:
  void push(std::complex<float> s, std::vector<float>* audio_out);

  FmDemodulatorConfig config_;
  Nco nco_;
  DecimatingComplexFir stage1_;   // 240k -> 48k, anti-alias only (wide transition)
  DecimatingComplexFir channel_;  // 48k channel filter: defines the 12.5 kHz channel
  std::complex<float> previous_{1.0f, 0.0f};
  DecimatingComplexFir audio_lpf_;  // 48k -> 12k (real data carried in .real())
  RealFir hf_noise_;                // high-pass of discriminator output (noise band)
  float deemph_alpha_ = 0.0f;
  float deemph_state_ = 0.0f;
  float dc_state_ = 0.0f;
  float noise_power_ = 0.0f;
  float noise_ratio_ = 1.0f;
  std::size_t noise_count_ = 0;
};

}  // namespace ghostnet::rf
