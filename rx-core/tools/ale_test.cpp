// 2G ALE decoder tests: Golay code, word codec, 8-FSK demodulation and word
// sync under noise and mistuning, false alarms on noise, and protocol
// assembly (soundings, calls, AMD).

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

#include "ghostnet/rx/ale.hpp"

using namespace ghostnet::rx::ale;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
  std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
  if (!ok) ++failures;
}

int popcount(std::uint32_t v) {
  int n = 0;
  for (; v; v &= v - 1) ++n;
  return n;
}

void golay_tests() {
  std::printf("== Golay (24,12) ==\n");
  // Reference values of the MIL-STD-188-141 extended Golay code (generator
  // 0xAE3), as also tabulated in LinuxALE's golay.c.
  check((golay_encode(1) & 0xFFF) == 0x5C7 && (golay_encode(2) & 0xFFF) == 0xB8D &&
            (golay_encode(0x800) & 0xFFF) == 0xAE3 && (golay_encode(0x400) & 0xFFF) == 0xF92,
        "check bits match the standard code's table");
  int min_w = 24;
  for (std::uint32_t d = 1; d < 4096; ++d) min_w = std::min(min_w, popcount(golay_encode(d)));
  check(min_w == 8, "minimum distance 8 (min weight " + std::to_string(min_w) + ")");

  std::mt19937 rng(5);
  bool all_fixed = true, all_detected = true;
  for (int trial = 0; trial < 20000; ++trial) {
    const std::uint16_t d = rng() & 0xFFF;
    const std::uint32_t cw = golay_encode(d);
    const int nerr = 1 + trial % 4;
    std::uint32_t e = 0;
    while (popcount(e) < nerr) e |= 1u << (rng() % 24);
    int errors = -1;
    const std::uint16_t out = golay_decode(cw ^ e, &errors);
    if (nerr <= 3 && (out != d || errors != nerr)) all_fixed = false;
    if (nerr == 4 && errors != 4) all_detected = false;
  }
  check(all_fixed, "corrects every 1-3 bit error pattern tried (15000)");
  check(all_detected, "flags every 4-bit error pattern tried as uncorrectable (5000)");
}

void codec_tests() {
  std::printf("== word codec ==\n");
  const std::uint32_t w = make_word(kTo, 'G', 'H', 'O');
  auto bits = word_bits(w);
  WordDecode d = decode_bits(bits.data());
  check(d.word == w && d.errors() == 0 && d.bad_votes == 0, "clean word round-trips");

  // Corrupt one whole copy: the majority vote must remove it entirely.
  for (int i = 0; i < 49; ++i) bits[49 + i] ^= 1;
  d = decode_bits(bits.data());
  check(d.word == w && d.errors() == 0 && d.bad_votes == 48, "a fully corrupted copy is outvoted");

  // Two copies wrong in 3 interleaved positions each half: Golay fixes it.
  bits = word_bits(w);
  for (int i : {0, 1, 4, 5, 10, 11}) {
    bits[i] ^= 1;
    bits[i + 98] ^= 1;
  }
  d = decode_bits(bits.data());
  check(d.word == w && d.errors_a == 3 && d.errors_b == 3, "3 voted-out errors per half corrected");

  // Inverted check bits of word B: a receiver that didn't invert would fail.
  const auto clean = word_bits(make_word(kTis, 'A', 'B', 'C'));
  check(decode_bits(clean.data()).word == make_word(kTis, 'A', 'B', 'C'), "B check-bit inversion round-trips");

  // Gray mapping: tone k carries tribit k ^ (k >> 1).
  const auto sym = word_symbols(make_word(kData, 0, 0, 0));
  check(sym[0] == 0, "all-zero bits are the 750 Hz tone");
}

std::vector<float> add_noise(std::vector<float> x, double sigma, unsigned seed) {
  std::mt19937 rng(seed);
  std::normal_distribution<double> n(0.0, sigma);
  for (auto& v : x) v += static_cast<float>(n(rng));
  return x;
}

std::vector<std::uint32_t> call_words() {
  // Scanning call to GHOSTNET (3 repeats), then the caller's TIS.
  std::vector<std::uint32_t> words;
  for (int i = 0; i < 3; ++i) {
    const auto to = address_words(kTo, "GHOSTNET");
    words.insert(words.end(), to.begin(), to.end());
  }
  const auto tis = address_words(kTis, "N0CALL");
  words.insert(words.end(), tis.begin(), tis.end());
  return words;
}

struct RunResult {
  std::vector<Word> words;
  std::vector<Transmission> tx;
};

RunResult run(const std::vector<float>& audio) {
  RunResult r;
  Assembler asmb([&](const Transmission& t) { r.tx.push_back(t); });
  Demodulator demod([&](const Word& w) {
    r.words.push_back(w);
    asmb.add(w);
  });
  demod.set_channel(7104500);
  for (std::size_t off = 0; off < audio.size(); off += 1000) {
    const std::size_t n = std::min<std::size_t>(1000, audio.size() - off);
    demod.process(audio.data() + off, n, static_cast<std::int64_t>(off / 12));
    asmb.tick(static_cast<std::int64_t>((off + n) / 12));
  }
  demod.flush();
  asmb.flush();
  return r;
}

void demod_tests() {
  std::printf("== demodulator and sync ==\n");
  const auto words = call_words();
  const double amp = 0.3;

  {
    const auto audio = synthesize_audio(words, 12000.0, amp, 0.537);
    const auto r = run(add_noise(audio, 0.01, 1));
    bool exact = r.words.size() == words.size();
    for (std::size_t i = 0; exact && i < words.size(); ++i) exact = r.words[i].raw == words[i];
    check(exact, "clean: every word decoded once, in order (" + std::to_string(r.words.size()) + "/" +
                     std::to_string(words.size()) + ")");
    check(r.tx.size() == 1 && r.tx[0].kind == Transmission::Kind::kCall && r.tx[0].from == "N0CALL" &&
              r.tx[0].to.size() == 1 && r.tx[0].to[0] == "GHOSTNET",
          "clean: assembled as 'N0CALL calling GHOSTNET'" + (r.tx.empty() ? "" : " -> " + r.tx[0].summary()));
    if (!r.words.empty()) {
      const double expect_ms = 537.0 + 392.0;
      check(std::abs(r.words[0].time_ms - expect_ms) < 12.0,
            "word timestamp accurate (" + std::to_string(r.words[0].time_ms) + " ms, expected ~929)");
    }
  }

  // Mistuning: tones up to +-40 Hz off.
  for (double off : {-40.0, 25.0, 40.0}) {
    const auto audio = synthesize_audio(words, 12000.0, amp, 0.3, off);
    const auto r = run(add_noise(audio, 0.01, 2));
    check(r.tx.size() == 1 && r.tx[0].kind == Transmission::Kind::kCall,
          "mistuned " + std::to_string(static_cast<int>(off)) + " Hz: call still assembled (" +
              std::to_string(r.words.size()) + " words)");
  }

  // SNR sweep. SNR in a 3 kHz bandwidth: signal power a^2/2, noise sigma^2
  // over 6 kHz (12 kHz real sampling), so in 3 kHz it is sigma^2/2.
  std::printf("  SNR (3 kHz)  words decoded  wrong words\n");
  int ok_at_6db = 0, wrong_total = 0;
  for (double snr_db : {10.0, 3.0, 0.0, -3.0, -6.0, -9.0}) {
    const double sigma = std::sqrt(amp * amp / 2.0 / std::pow(10.0, snr_db / 10.0) * 2.0);
    int decoded = 0, wrong = 0;
    for (unsigned seed = 0; seed < 5; ++seed) {
      const auto r = run(add_noise(synthesize_audio(words, 12000.0, amp, 0.2 + 0.013 * seed), sigma, 100 + seed));
      for (const auto& w : r.words) {
        bool known = false;
        for (auto x : words) known |= x == w.raw;
        (known ? decoded : wrong)++;
      }
    }
    std::printf("  %6.1f dB    %3d / %3zu      %d\n", snr_db, decoded, 5 * words.size(), wrong);
    if (snr_db == 3.0) ok_at_6db = decoded;
    wrong_total += wrong;
  }
  check(ok_at_6db >= static_cast<int>(5 * words.size() * 9 / 10), "at +3 dB SNR at least 90% of words decode");
  check(wrong_total == 0, "no wrong words reported at any SNR");

  // False alarms: 10 minutes of noise, plus a steady carrier and a slow
  // tone sweep (things a scanning receiver meets on every band).
  {
    std::vector<float> noise(12000 * 600);
    noise = add_noise(noise, 0.3, 77);
    for (std::size_t i = 0; i < 12000 * 60; ++i) noise[i] += 0.5f * std::sin(6.2831853 * 1000.0 * i / 12000.0);
    for (std::size_t i = 12000 * 60; i < 12000 * 120; ++i)
      noise[i] += 0.5f * std::sin(6.2831853 * (750.0 + (i % 12000) / 12000.0 * 1750.0) * i / 12000.0);
    const auto r = run(noise);
    check(r.words.empty(), "10 min of noise, carrier and sweep: " + std::to_string(r.words.size()) + " false words");
  }
}

void assembler_tests() {
  std::printf("== protocol assembly ==\n");
  auto feed = [](const std::vector<std::uint32_t>& raw, std::int64_t t0 = 1000, std::uint32_t ch = 7104500) {
    std::vector<Transmission> out;
    Assembler a([&](const Transmission& t) { out.push_back(t); });
    std::int64_t t = t0;
    for (auto r : raw) {
      Word w;
      w.raw = r;
      w.preamble = static_cast<int>(r >> 21);
      w.chars[0] = static_cast<char>((r >> 14) & 0x7F);
      w.chars[1] = static_cast<char>((r >> 7) & 0x7F);
      w.chars[2] = static_cast<char>(r & 0x7F);
      w.time_ms = t;
      w.channel_hz = ch;
      t += 392;
      a.add(w);
    }
    a.flush();
    return out;
  };

  std::vector<std::uint32_t> snd;
  for (int i = 0; i < 4; ++i) {
    auto w = address_words(kTws, "KX4ABC");
    snd.insert(snd.end(), w.begin(), w.end());
  }
  auto t = feed(snd);
  check(t.size() == 1 && t[0].kind == Transmission::Kind::kSounding && t[0].from == "KX4ABC" && t[0].tws,
        "TWS sounding" + (t.empty() ? "" : " -> " + t[0].summary()));

  std::vector<std::uint32_t> amd = address_words(kTo, "GHOSTNET");
  for (const char* s : {"NET", " CH", "ECK", " IN"}) {
    amd.push_back(make_word(amd.size() == 3 ? kCmd : (amd.size() % 2 ? kData : kRep), s[0], s[1], s[2]));
  }
  const auto tis = address_words(kTis, "N0CALL");
  amd.insert(amd.end(), tis.begin(), tis.end());
  t = feed(amd);
  check(t.size() == 1 && t[0].kind == Transmission::Kind::kCall && t[0].amd == "NET CHECK IN",
        "call with AMD" + (t.empty() ? "" : " -> " + t[0].summary()));

  t = feed(address_words(kTo, "GHOSTNET"));
  check(t.size() == 1 && t[0].kind == Transmission::Kind::kCallPartial, "TO without TIS is a partial call");

  std::vector<std::uint32_t> lqa = address_words(kTo, "ABC");
  lqa.push_back(make_word(kCmd, 'a', 0x01, 0x7F));
  auto tis2 = address_words(kTis, "XYZ");
  lqa.insert(lqa.end(), tis2.begin(), tis2.end());
  t = feed(lqa);
  check(t.size() == 1 && t[0].other_commands.size() == 1 && t[0].amd.empty(),
        "non-AMD command reported raw, not guessed" + (t.empty() ? "" : " -> " + t[0].summary()));

  // A gap splits transmissions.
  std::vector<Transmission> two;
  Assembler a([&](const Transmission& x) { two.push_back(x); });
  Word w;
  w.preamble = kTis;
  std::snprintf(w.chars, sizeof w.chars, "AAA");
  w.time_ms = 0;
  a.add(w);
  w.time_ms = 5000;
  a.add(w);
  a.flush();
  check(two.size() == 2, "a 5 s gap starts a new transmission");
}

}  // namespace

int main() {
  golay_tests();
  codec_tests();
  demod_tests();
  assembler_tests();
  if (failures) {
    std::fprintf(stderr, "\n%d check(s) failed\n", failures);
    return EXIT_FAILURE;
  }
  std::printf("\nall checks passed\n");
  return EXIT_SUCCESS;
}
