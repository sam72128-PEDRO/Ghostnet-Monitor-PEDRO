#pragma once

#include <complex>
#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

#include "ghostnet/rf/fir.hpp"
#include "ghostnet/rf/nco.hpp"

namespace ghostnet::rf {

enum class Sideband { kUsb, kLsb };

struct SsbDemodulatorConfig {
  // Rate of the samples handed to process_*(). For RTL2832U capture this is
  // the dongle sample rate (default pipeline: 240 kS/s). Must be an integer
  // multiple of audio_sample_rate_hz.
  double input_sample_rate_hz = 240000.0;

  // Output rate. js8core's receive contract is 12 kHz mono.
  double audio_sample_rate_hz = 12000.0;

  // Where the dial (suppressed-carrier) frequency sits relative to the
  // centre of the input stream: dial_hz - capture_center_hz. Tuning the
  // dongle a little away from the dial keeps the RTL2832U's DC/LO artefact
  // out of the audio passband.
  double tune_offset_hz = 0.0;

  Sideband sideband = Sideband::kUsb;

  // Conjugate the input (swap the meaning of +f / -f) before anything else.
  // Exposed because whether RTL2832U Q-branch direct sampling delivers an
  // inverted spectrum has not been verified on hardware yet; if JS8 never
  // decodes on a known-good signal, this is the first switch to flip.
  bool invert_spectrum = false;

  // Upper audio edge relative to the dial. JS8's normal sub-band is about
  // 200-2500 Hz above the dial.
  double passband_high_hz = 2800.0;

  // Final channel filter taps (at 4x the audio rate when two-stage
  // decimation is used, otherwise at the input rate).
  std::size_t lowpass_taps = 127;

  // Hilbert transformer taps, applied at the audio rate.
  std::size_t hilbert_taps = 129;
};

// Single-sideband receiver back end: input stream (complex IQ, or real
// samples from a single-ADC front end) -> demodulated audio at a fixed rate.
//
// Signal path: optional conjugate -> complex mix by tune_offset_hz -> one or
// two decimating low-pass stages -> phasing-method sideband selection
// (USB = I - H{Q}, LSB = I + H{Q}) at the audio rate.
//
// The low-pass stages have real coefficients, so their magnitude response
// is symmetric about 0 Hz: they band-limit but cannot reject the opposite
// sideband. Sideband rejection comes only from the final Hilbert combine.
// An earlier version applied the Hilbert transform before the mix and
// measured 0 dB image rejection; tools/image_rejection_test.cpp exists to
// keep that from coming back.
class SsbDemodulator {
 public:
  explicit SsbDemodulator(SsbDemodulatorConfig config);

  // RTL2832U wire format: interleaved unsigned 8-bit I,Q pairs, 127.5 = 0.
  void process_iq_u8(const std::uint8_t* iq, std::size_t pairs, std::vector<float>* audio_out);

  void process_complex(const std::complex<float>* samples, std::size_t count, std::vector<float>* audio_out);

  // Real samples (e.g. a single-ADC front end or a synthetic fixture).
  void process_real_i16(const std::int16_t* samples, std::size_t count, std::vector<float>* audio_out);

  const SsbDemodulatorConfig& config() const { return config_; }

  // Total delay, in output samples, from an input sample to its effect on
  // the audio. Used to back-date audio timestamps for JS8 slot alignment.
  double latency_seconds() const;

 private:
  void push(std::complex<float> sample, std::vector<float>* audio_out);

  SsbDemodulatorConfig config_;
  Nco nco_;
  std::unique_ptr<DecimatingComplexFir> stage1_;  // null when single-stage
  DecimatingComplexFir stage2_;
  RealFir hilbert_;
  std::vector<float> i_delay_;
  std::size_t i_delay_pos_ = 0;
};

// Slow automatic gain control for feeding a decoder that wants int16 PCM.
// Tracks the mean-square level with a long time constant and steers it to a
// fixed target, so weak-signal audio is not lost to int16 quantisation and
// strong signals do not clip. Deliberately slow (seconds) so it does not
// pump within a JS8 transmission.
class SlowAgc {
 public:
  explicit SlowAgc(double sample_rate_hz, double target_rms = 3000.0, double time_constant_s = 2.0,
                   double max_gain = 1.0e6);

  // Applies gain in place and writes clamped int16 samples to `out`.
  void process(const float* in, std::size_t count, std::vector<std::int16_t>* out);

  // Pre-gain input level in dBFS (relative to a full-scale sine of 1.0),
  // useful as a "signal present" indicator.
  double input_level_dbfs() const;
  double gain() const { return gain_; }

 private:
  double alpha_;
  double target_rms_;
  double max_gain_;
  double mean_square_ = 0.0;
  double gain_ = 1.0;
  bool primed_ = false;
};

}  // namespace ghostnet::rf
