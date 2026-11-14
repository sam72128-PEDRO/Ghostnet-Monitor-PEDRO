#include "ghostnet/rx/rtty_test_signal.hpp"

#include <cmath>

#include "ghostnet/rx/rtty.hpp"

namespace ghostnet::rx {

namespace {
// Reverse lookup of RttyDecoder's tables: (code, needs_figures).
bool encode_char(char c, int* code, bool* figures) {
  for (int f = 0; f < 2; ++f) {
    for (int k = 0; k < 32; ++k) {
      if (k == 0x1B || k == 0x1F) continue;
      if (RttyDecoder::decode_baudot(k, f == 1) == c) {
        *code = k;
        // Characters common to both sets (space, CR, LF) don't need a shift.
        *figures = (f == 1) && RttyDecoder::decode_baudot(k, false) != c;
        return true;
      }
    }
  }
  return false;
}
}  // namespace

std::vector<float> rtty_frequency_track(const std::string& text, const RttyTestParams& p) {
  const double spb = p.sample_rate_hz / p.baud;
  const double hi = p.center_hz + p.shift_hz / 2.0;
  const double lo = p.center_hz - p.shift_hz / 2.0;
  const float mark = static_cast<float>(p.reverse ? lo : hi);
  const float space = static_cast<float>(p.reverse ? hi : lo);
  std::vector<float> f;
  double t = 0.0;  // fractional sample clock so bit timing doesn't drift
  auto emit = [&](float hz, double bits) {
    t += bits * spb;
    while (static_cast<double>(f.size()) < t) f.push_back(hz);
  };
  emit(mark, p.lead_in_s * p.baud);

  std::vector<int> codes;
  bool figures = false;
  codes.push_back(0x1F);  // start in LTRS
  for (char c : text) {
    int code = 0;
    bool needs_figs = false;
    if (!encode_char(c, &code, &needs_figs)) continue;
    const bool shared = RttyDecoder::decode_baudot(code, true) == RttyDecoder::decode_baudot(code, false);
    if (!shared && needs_figs != figures) {
      codes.push_back(needs_figs ? 0x1B : 0x1F);
      figures = needs_figs;
    }
    codes.push_back(code);
    if (c == ' ') figures = false;  // receiver unshifts on space
  }
  int index = 0;
  for (int code : codes) {
    emit(space, 1.0);  // start
    for (int b = 0; b < 5; ++b) emit((code >> b) & 1 ? mark : space, 1.0);
    emit(index == p.break_stop_bit_at ? space : mark, 1.5);  // stop
    ++index;
  }
  emit(mark, p.baud * 1.0);  // trailing idle
  return f;
}

std::vector<float> fsk_audio(const std::vector<float>& freq, double sample_rate_hz, double amplitude) {
  std::vector<float> out(freq.size());
  double phase = 0.0;
  for (std::size_t i = 0; i < freq.size(); ++i) {
    phase = std::remainder(phase + 2.0 * 3.14159265358979323846 * freq[i] / sample_rate_hz, 2.0 * 3.14159265358979323846);
    out[i] = freq[i] > 0 ? static_cast<float>(amplitude * std::cos(phase)) : 0.0f;
  }
  return out;
}

}  // namespace ghostnet::rx
