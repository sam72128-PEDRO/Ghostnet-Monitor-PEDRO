#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ghostnet::rx {

// Test-signal generation for RTTY (host tests and tools only; nothing on the
// receive path uses it). Produces continuous-phase AFSK audio of `text` at
// 45.45 baud, 1 start / 5 data / 1.5 stop bits, US-TTY Baudot with LTRS/FIGS
// shifts, preceded by idle mark.
struct RttyTestParams {
  double sample_rate_hz = 12000.0;
  double baud = 45.45;
  double center_hz = 1615.0;
  double shift_hz = 170.0;
  bool reverse = false;          // transmit with mark/space swapped
  double lead_in_s = 1.5;        // idle mark before the text
  double amplitude = 0.3;
  int break_stop_bit_at = -1;    // character index whose stop bit is forced to space
};

// Instantaneous audio frequency for every sample (0 = silence).
std::vector<float> rtty_frequency_track(const std::string& text, const RttyTestParams& p);

// Audio samples from a frequency track (continuous phase).
std::vector<float> fsk_audio(const std::vector<float>& freq, double sample_rate_hz, double amplitude);

}  // namespace ghostnet::rx
