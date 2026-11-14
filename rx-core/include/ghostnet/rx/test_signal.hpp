#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "ghostnet/rx/receiver.hpp"

namespace ghostnet::rx {

inline constexpr int kJs8Symbols = 79;
using Js8Tones = std::array<int, kJs8Symbols>;

// Parameters for a synthetic RTL-SDR capture containing a JS8 Normal-mode
// transmission. Signal and noise are in u8 IQ counts (full scale 127.5).
struct TestSignalParams {
  double audio_hz = 1500.0;      // offset above the dial
  double signal_counts = 3.0;    // tone amplitude
  double noise_counts = 4.0;     // Gaussian sigma per axis
  double interferer_counts = 30.0;  // carrier 9 kHz above the dial (selectivity check)
  double dc_i_counts = 1.5;      // DC offset, as on real dongles
  double dc_q_counts = -1.0;
  bool mirrored = false;         // deliver a spectrum-inverted capture
  std::uint32_t sample_rate_hz = 240000;
  std::int32_t capture_offset_hz = 25000;  // dongle tuned this far above the dial
  std::uint32_t seed = 1234;
};

struct TestSignal {
  std::vector<std::uint8_t> iq;          // interleaved u8 I/Q
  std::int64_t start_unix_ms = 0;        // UTC of the first sample
  std::int64_t first_slot_unix_ms = 0;   // UTC slot the first frame is sent in
  std::size_t frames = 0;
};

// One frame per consecutive 15 s Normal-mode slot, starting 2 s into the
// capture, placed in recent past UTC so slot alignment behaves as live.
// Only generates an in-memory sample buffer; it does not, and cannot, drive
// any output device.
TestSignal synthesize_test_signal(const std::vector<Js8Tones>& frames, const TestSignalParams& params);

struct SelfTestResult {
  bool passed = false;
  std::string expected_text;
  std::string decoded_text;
  std::size_t frames_expected = 0;
  std::size_t frames_decoded = 0;
  double capture_seconds = 0.0;
  double wall_seconds = 0.0;
  std::string detail;
};

// Runs the full receive chain (SSB demod, AGC, JS8 decoder, unpack,
// assembly) over a fixed embedded test transmission ("N0CALL: @GHOSTNET
// SELF TEST ..."), replayed at `speed` x real time. Proves the native stack
// works on this device without any radio attached. Blocks for roughly
// capture_seconds / speed (about 8 s at the default speed).
SelfTestResult run_self_test(double speed = 10.0);

}  // namespace ghostnet::rx
