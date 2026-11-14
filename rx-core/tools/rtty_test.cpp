// RTTY decoder checks on synthetic 45.45 baud AFSK audio: auto-centring on
// an off-centre signal in noise, reversed polarity, silence on noise, and
// framing errors reported rather than guessed.

#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

#include "ghostnet/rx/rtty.hpp"
#include "ghostnet/rx/rtty_test_signal.hpp"

using namespace ghostnet::rx;

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
  if (!ok) ++failures;
}

const std::string kText = "RYRYRY CQ GHOSTNET RTTY TEST DE N0CALL 0123 456789 ?/.-\n";

struct Decoded {
  std::string chars;
  std::vector<RttyLine> lines;
  double center = 0.0;
  bool locked = false;
};

Decoded decode(const std::vector<float>& audio, RttyConfig cfg) {
  Decoded d;
  RttyDecoder dec(
      cfg, [&](const std::string& s, bool) { d.chars += s; }, [&](const RttyLine& l) { d.lines.push_back(l); });
  const std::size_t chunk = 409;  // what one 16 KiB USB transfer becomes at 12 kHz
  for (std::size_t off = 0; off < audio.size(); off += chunk) {
    dec.process(audio.data() + off, std::min(chunk, audio.size() - off), static_cast<std::int64_t>(off / 12));
  }
  dec.flush();
  d.center = dec.center_hz();
  d.locked = dec.locked();
  return d;
}

std::vector<float> with_noise(std::vector<float> a, double sigma, std::uint32_t seed = 3) {
  std::mt19937 rng(seed);
  std::normal_distribution<double> n(0.0, sigma);
  for (auto& v : a) v += static_cast<float>(n(rng));
  return a;
}

std::string trim(std::string s) {
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
  return s;
}
}  // namespace

int main() {
  const std::string expected = trim(kText);
  // With auto-centring the first ~1 s of preamble passes before lock, which
  // is why RTTY transmissions open with RYRY / idle; everything after it must
  // be exact.
  const std::string after_preamble = expected.substr(expected.find("CQ"));

  {
    RttyTestParams p;
    p.center_hz = 1615.0;
    auto audio = with_noise(fsk_audio(rtty_frequency_track(kText, p), 12000.0, 0.3), 0.15);
    auto d = decode(audio, RttyConfig{});
    std::printf("     auto centre %.1f Hz, text: '%s'\n", d.center, trim(d.chars).c_str());
    check(std::abs(d.center - 1615.0) < 8.0, "auto-centre finds the tone pair (1615 Hz, within 8 Hz)");
    check(trim(d.chars).find(after_preamble) != std::string::npos, "decodes everything after the preamble exactly, in noise");
    check(d.lines.size() == 1 && trim(d.lines[0].text).find(after_preamble) != std::string::npos &&
              d.lines[0].framing_errors == 0,
          "one stored line, no framing errors");
  }

  {
    RttyTestParams p;
    p.reverse = true;
    auto audio = with_noise(fsk_audio(rtty_frequency_track(kText, p), 12000.0, 0.3), 0.1);
    RttyConfig rev;
    rev.reverse = true;
    auto good = decode(audio, rev);
    auto bad = decode(audio, RttyConfig{});
    check(trim(good.chars).find(after_preamble) != std::string::npos, "reversed signal decodes with Reverse on");
    check(trim(bad.chars).find("GHOSTNET") == std::string::npos, "reversed signal does not decode with Reverse off");
  }

  {
    std::vector<float> noise(12000 * 20, 0.0f);
    auto d = decode(with_noise(noise, 0.2), RttyConfig{});
    std::printf("     noise only: %zu characters, locked=%d\n", d.chars.size(), d.locked);
    check(d.chars.size() < 3 && d.lines.empty(), "pure noise prints (almost) nothing");
    RttyConfig fixed;
    fixed.center_hz = 1500.0;
    auto f = decode(with_noise(noise, 0.2, 9), fixed);
    std::printf("     noise only, fixed centre: %zu characters\n", f.chars.size());
    check(f.chars.size() < 3 && f.lines.empty(), "pure noise prints (almost) nothing with a fixed centre either");
  }

  {
    RttyTestParams p;
    p.break_stop_bit_at = 12;
    auto audio = with_noise(fsk_audio(rtty_frequency_track(kText, p), 12000.0, 0.3), 0.05);
    RttyConfig fixed;
    fixed.center_hz = p.center_hz;
    auto d = decode(audio, fixed);
    std::printf("     with a broken stop bit: '%s'\n", trim(d.chars).c_str());
    check(d.chars.find('~') != std::string::npos, "framing error shown as '~', not guessed");
    check(trim(d.chars).find("TEST DE N0CALL 0123 456789") != std::string::npos, "decoder resynchronises after the error");
    check(trim(d.chars).rfind("RYRYRY CQ", 0) == 0, "fixed centre decodes from the very first character");
  }

  if (failures) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return EXIT_FAILURE;
  }
  std::printf("all checks passed\n");
  return EXIT_SUCCESS;
}
