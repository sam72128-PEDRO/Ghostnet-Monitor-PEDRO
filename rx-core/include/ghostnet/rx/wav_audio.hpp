#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ghostnet::rx {

struct WavInfo {
  int sample_rate_hz = 0;
  int channels = 0;
  int bits = 0;
  double seconds = 0.0;
};

// Reads a PCM WAV file (8/16/24/32-bit integer or 32-bit float, any number
// of channels, any sample rate), mixes it to mono and resamples it to
// `out_rate_hz` (anti-alias filtered). Values are scaled to +-1. Used to run
// real off-air recordings through the decoders. Returns false with *error
// set if the file can't be read.
bool read_wav_mono(const std::string& path, double out_rate_hz, std::vector<float>* out, WavInfo* info,
                   std::string* error);

// Writes 16-bit mono PCM (tests).
bool write_wav_mono16(const std::string& path, const std::vector<float>& samples, int rate_hz);

}  // namespace ghostnet::rx
