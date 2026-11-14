#pragma once

#include <cstdint>
#include <deque>
#include <vector>

#include "ghostnet/rf/fir.hpp"
#include "ghostnet/rf/nco.hpp"

namespace ghostnet::rf {

enum class Sideband { kUsb, kLsb };

struct SsbDemodulatorConfig {
  // Sample rate of the incoming real-valued samples (e.g. from RTL2832U
  // Q-branch direct sampling). Must be an integer multiple of
  // audio_sample_rate_hz; this is a simplification, not a hardware fact —
  // real capture rates must be chosen (or resampled) to satisfy it.
  double input_sample_rate_hz = 48000.0;

  // Output audio rate. js8core's documented receive contract is 12 kHz
  // mono (see third_party/js8core include/js8core/decoder_state.hpp,
  // kJs8RxSampleRate); other decoders may need a different rate, so this
  // is a parameter, not a hardcoded constant.
  double audio_sample_rate_hz = 12000.0;

  // Tuned offset, in Hz, of the desired passband's suppressed-carrier
  // reference relative to the input stream's zero frequency. For a real
  // RTL-SDR capture this corresponds to (dial_frequency - capture_center_
  // frequency); computing that mapping from an actual tuned RTL2832U
  // configuration is integration work the demodulator itself does not do.
  double tune_offset_hz = 0.0;

  Sideband sideband = Sideband::kUsb;

  // Audio passband edge, Hz, relative to the suppressed-carrier reference.
  // JS8's default sub-band is roughly 200-2500 Hz (see GhostNetMonitor
  // phase0 JNI defaults); pick something that comfortably covers that with
  // a little margin, and leave the low edge at the sideband's own skirt
  // (we filter only on the high side here; see note in ssb_demodulator.cpp
  // on why the Hilbert branch already suppresses true DC/near-DC leakage
  // from the opposite sideband rather than needing a separate high-pass).
  double passband_high_hz = 2800.0;

  // FIR tap counts. Odd numbers. Larger = sharper skirts / better image
  // rejection, at the cost of more per-sample work and more group delay.
  // These are host-validated against the synthetic fixture in
  // tools/image_rejection_test.cpp, not against real RF; they are a
  // starting point for hardware tuning, not a final answer.
  std::size_t hilbert_taps = 129;
  std::size_t lowpass_taps = 127;
};

// Converts a stream of real-valued samples (as produced by RTL2832U
// Q-branch direct sampling, or any other real single-ADC-channel receiver
// front end) into demodulated single-sideband audio at a fixed output
// rate.
//
// This class implements the DSP only. It has no knowledge of USB, the
// RTL2832U register set, or Android — see WS3 in PHASE1_PLAN.md for how it
// is meant to be wired into the actual capture path. Validate it against
// the synthetic fixture in tools/image_rejection_test.cpp before trusting
// it with a live antenna.
class SsbDemodulator {
 public:
  explicit SsbDemodulator(SsbDemodulatorConfig config);

  // Feeds `count` real input samples and appends any audio samples that
  // became available (decimation means not every call produces output).
  void process(const std::int16_t* samples, std::size_t count, std::vector<float>* audio_out);

 private:
  // Order of operations, and why: complex-mix the raw real samples down
  // to baseband first, then band-limit with an ordinary (sideband-
  // agnostic) complex low-pass and decimate — this stage only needs to
  // reject what's far from the tuned reference, which a real-coefficient
  // filter does fine (its magnitude response is necessarily symmetric
  // around 0 Hz, since real coefficients make H(-f) = conj(H(f)); that
  // symmetry means it *cannot* tell +f apart from -f, i.e. it cannot by
  // itself separate the wanted sideband from the image). Sideband
  // separation happens afterwards, on the decimated I/Q pair, via the
  // classic phasing-method combine: Hilbert-transform one branch and
  // add/subtract from the delay-matched other branch. Doing the Hilbert
  // step before the mix (an earlier version of this file did that) does
  // not work: see tools/image_rejection_test.cpp, which caught it at 0 dB
  // rejection.
  SsbDemodulatorConfig config_;
  ComplexFir lowpass_fir_;
  RealFir hilbert_fir_;
  Nco nco_;
  std::deque<double> i_delay_line_;
  std::size_t decimation_factor_;
  std::size_t decimation_counter_ = 0;
};

}  // namespace ghostnet::rf
