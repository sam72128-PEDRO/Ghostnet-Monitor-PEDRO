#include "ghostnet/rx/ale.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ghostnet::rx::ale {

namespace {

constexpr double kTwoPi = 6.283185307179586;
constexpr std::uint32_t kGenerator = 0xAE3;  // x^11 + x^9 + x^7 + x^6 + x^5 + x + 1
constexpr int kPhases = 8;

// Acquisition and continuation thresholds (see the class comment).
constexpr int kAcquireMaxErrors = 1;
constexpr int kAcquireMaxBadVotes = 20;
constexpr int kContinueMaxBadVotes = 30;
constexpr int kMaxMisses = 2;

int popcount(std::uint32_t v) {
  int n = 0;
  for (; v; v &= v - 1) ++n;
  return n;
}

struct GolayTables {
  std::array<std::uint16_t, 4096> check{};
  std::array<std::uint32_t, 4096> error{};  // syndrome -> error pattern; 0xFFFFFFFF = uncorrectable

  GolayTables() {
    for (std::uint32_t d = 0; d < 4096; ++d) {
      std::uint32_t r = d << 11;
      for (int i = 22; i >= 11; --i) {
        if (r >> i & 1u) r ^= kGenerator << (i - 11);
      }
      const std::uint32_t cw23 = (d << 11) | r;
      check[d] = static_cast<std::uint16_t>((r << 1) | (popcount(cw23) & 1));
    }
    error.fill(0xFFFFFFFFu);
    auto add = [this](std::uint32_t e) {
      const std::uint32_t s = (e & 0xFFFu) ^ check[e >> 12];
      if (error[s] == 0xFFFFFFFFu || popcount(error[s]) > popcount(e)) error[s] = e;
    };
    add(0);
    for (int a = 0; a < 24; ++a) {
      add(1u << a);
      for (int b = a + 1; b < 24; ++b) {
        add((1u << a) | (1u << b));
        for (int c = b + 1; c < 24; ++c) add((1u << a) | (1u << b) | (1u << c));
      }
    }
  }
};

const GolayTables& tables() {
  static const GolayTables t;
  return t;
}

int gray(int k) { return k ^ (k >> 1); }
int ungray(int v) {
  for (int k = 0; k < kTones; ++k) {
    if (gray(k) == v) return k;
  }
  return 0;
}

std::string strip_fill(std::string s) {
  while (s.size() > 1 && s.back() == '@') s.pop_back();
  return s;
}

}  // namespace

const char* preamble_name(int p) {
  static const char* names[] = {"DATA", "THRU", "TO", "TWS", "FROM", "TIS", "CMD", "REP"};
  return (p >= 0 && p < 8) ? names[p] : "?";
}

std::uint32_t golay_encode(std::uint16_t data12) {
  const std::uint16_t d = data12 & 0xFFF;
  return (static_cast<std::uint32_t>(d) << 12) | tables().check[d];
}

std::uint16_t golay_decode(std::uint32_t codeword, int* errors) {
  const auto& t = tables();
  const std::uint16_t data = (codeword >> 12) & 0xFFF;
  const std::uint16_t syndrome = (codeword & 0xFFF) ^ t.check[data];
  const std::uint32_t e = t.error[syndrome];
  if (e == 0xFFFFFFFFu) {
    if (errors) *errors = 4;
    return data;
  }
  if (errors) *errors = popcount(e);
  return static_cast<std::uint16_t>(data ^ (e >> 12));
}

std::uint32_t make_word(int preamble, char a, char b, char c) {
  return (static_cast<std::uint32_t>(preamble & 7) << 21) | (static_cast<std::uint32_t>(a & 0x7F) << 14) |
         (static_cast<std::uint32_t>(b & 0x7F) << 7) | static_cast<std::uint32_t>(c & 0x7F);
}

std::array<std::uint8_t, 3 * kSymbolsPerWord> word_bits(std::uint32_t word) {
  const std::uint32_t ca = golay_encode((word >> 12) & 0xFFF);
  const std::uint32_t cb = golay_encode(word & 0xFFF) ^ 0xFFFu;
  std::array<std::uint8_t, 3 * kSymbolsPerWord> bits{};
  for (int rep = 0; rep < 3; ++rep) {
    std::uint8_t* out = bits.data() + rep * kSymbolsPerWord;
    for (int i = 0; i < 24; ++i) {
      out[2 * i] = (ca >> (23 - i)) & 1u;
      out[2 * i + 1] = (cb >> (23 - i)) & 1u;
    }
    out[48] = 0;  // stuff bit
  }
  return bits;
}

std::array<std::uint8_t, kSymbolsPerWord> word_symbols(std::uint32_t word) {
  const auto bits = word_bits(word);
  std::array<std::uint8_t, kSymbolsPerWord> sym{};
  for (int s = 0; s < kSymbolsPerWord; ++s) {
    const int v = (bits[3 * s] << 2) | (bits[3 * s + 1] << 1) | bits[3 * s + 2];
    sym[s] = static_cast<std::uint8_t>(ungray(v));
  }
  return sym;
}

WordDecode decode_bits(const std::uint8_t* bits) {
  WordDecode r;
  std::uint32_t ca = 0, cb = 0;
  int bad = 0;
  for (int i = 0; i < 48; ++i) {
    const int sum = bits[i] + bits[i + kSymbolsPerWord] + bits[i + 2 * kSymbolsPerWord];
    bad += (sum == 1 || sum == 2);
    const std::uint32_t bit = sum >= 2 ? 1u : 0u;
    if (i % 2 == 0) ca = (ca << 1) | bit;
    else cb = (cb << 1) | bit;
  }
  cb ^= 0xFFFu;
  const std::uint16_t a = golay_decode(ca, &r.errors_a);
  const std::uint16_t b = golay_decode(cb, &r.errors_b);
  r.word = (static_cast<std::uint32_t>(a) << 12) | b;
  r.bad_votes = bad;
  return r;
}

bool is_basic38(char c) { return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '@' || c == '?'; }
bool is_ascii64(char c) { return c >= 0x20 && c <= 0x5F; }

std::string Word::text() const { return std::string(preamble_name(preamble)) + " " + chars; }

// ---- Demodulator -----------------------------------------------------------

Demodulator::Demodulator(WordSink sink, double sample_rate_hz)
    : sink_(std::move(sink)),
      rate_(sample_rate_hz),
      samples_per_symbol_(static_cast<int>(std::lround(sample_rate_hz * kSymbolSeconds))),
      phase_step_(std::max(1, samples_per_symbol_ / kPhases)),
      phases_(kPhases) {
  for (int k = 0; k < kTones; ++k) {
    const double w = kTwoPi * (kFirstToneHz + kToneStepHz * k) / rate_;
    step_[k] = {std::cos(w), -std::sin(w)};
    osc_[k] = {1.0, 0.0};
  }
  hist_.assign(static_cast<std::size_t>(samples_per_symbol_), {});
}

void Demodulator::reset() {
  flush_candidate(sample_index_, true);
  for (auto& p : phases_) p = Phase{};
  for (auto& h : hist_) h = {};
  sum_.fill({0.0, 0.0});
  reset_index_ = sample_index_;
  quality_ = 0.0f;
}

bool Demodulator::in_sync() const {
  return std::any_of(phases_.begin(), phases_.end(), [](const Phase& p) { return p.synced; });
}

std::int64_t Demodulator::sample_ms(std::int64_t n) const {
  return first_ms_ + static_cast<std::int64_t>(std::llround(static_cast<double>(n - first_index_) * 1000.0 / rate_));
}

void Demodulator::process(const float* audio, std::size_t count, std::int64_t start_ms) {
  first_index_ = sample_index_;
  first_ms_ = start_ms;
  for (std::size_t i = 0; i < count; ++i) {
    const double x = audio[i];
    auto& slot = hist_[hist_pos_];
    for (int k = 0; k < kTones; ++k) {
      const std::complex<double> y = x * osc_[k];
      osc_[k] *= step_[k];
      sum_[k] += y - std::complex<double>(slot[k]);
      slot[k] = std::complex<float>(y);
    }
    hist_pos_ = (hist_pos_ + 1) % hist_.size();
    ++sample_index_;

    if (++renorm_ >= 4096) {
      renorm_ = 0;
      for (int k = 0; k < kTones; ++k) {
        osc_[k] /= std::abs(osc_[k]);
        std::complex<double> s{0, 0};
        for (const auto& h : hist_) s += std::complex<double>(h[k]);
        sum_[k] = s;
      }
    }

    const std::int64_t since = sample_index_ - reset_index_;
    if (since < samples_per_symbol_ || since % phase_step_ != 0) continue;
    int best = 0;
    double best_p = -1.0, total = 0.0;
    for (int k = 0; k < kTones; ++k) {
      const double p = std::norm(sum_[k]);
      total += p;
      if (p > best_p) {
        best_p = p;
        best = k;
      }
    }
    const int phase = static_cast<int>((since / phase_step_) % kPhases);
    if (phase == 0 && total > 0.0) quality_ = 0.98f * quality_ + 0.02f * static_cast<float>(best_p / total);
    on_symbol(phases_[phase], best, sample_index_);
    flush_candidate(sample_index_, false);
  }
}

void Demodulator::on_symbol(Phase& ph, int tone, std::int64_t n) {
  ph.symbols[ph.head] = static_cast<std::uint8_t>(tone);
  ph.head = (ph.head + 1) % kSymbolsPerWord;
  if (ph.count < kSymbolsPerWord) ++ph.count;
  ++ph.since_word;
  if (ph.count < kSymbolsPerWord) return;

  std::uint8_t bits[3 * kSymbolsPerWord];
  for (int s = 0; s < kSymbolsPerWord; ++s) {
    const int v = gray(ph.symbols[(ph.head + s) % kSymbolsPerWord]);
    bits[3 * s] = (v >> 2) & 1;
    bits[3 * s + 1] = (v >> 1) & 1;
    bits[3 * s + 2] = v & 1;
  }
  const WordDecode d = decode_bits(bits);

  Word w;
  w.raw = d.word;
  w.preamble = static_cast<int>((d.word >> 21) & 7);
  w.chars[0] = static_cast<char>((d.word >> 14) & 0x7F);
  w.chars[1] = static_cast<char>((d.word >> 7) & 0x7F);
  w.chars[2] = static_cast<char>(d.word & 0x7F);
  w.errors = d.errors();
  w.bad_votes = d.bad_votes;
  w.time_ms = sample_ms(n);
  w.channel_hz = channel_hz_;
  const int score = 8 * (d.errors_a + d.errors_b) + d.bad_votes;

  if (ph.synced && ph.since_word == kSymbolsPerWord) {
    ph.since_word = 0;
    if (d.correctable() && d.bad_votes <= kContinueMaxBadVotes) {
      ph.misses = 0;
      w.acquired = false;
      offer(w, score, n);
    } else if (++ph.misses >= kMaxMisses) {
      ph.synced = false;
    }
    return;
  }
  if (ph.synced && ph.since_word > kSymbolsPerWord) ph.synced = false;

  if (d.errors_a > kAcquireMaxErrors || d.errors_b > kAcquireMaxErrors || d.bad_votes > kAcquireMaxBadVotes) return;
  bool plausible = false;
  switch (w.preamble) {
    case kTo: case kTis: case kTws: case kThru: case kFrom:
      plausible = is_basic38(w.chars[0]) && is_basic38(w.chars[1]) && is_basic38(w.chars[2]);
      break;
    case kCmd:
      plausible = is_ascii64(w.chars[0]) && is_ascii64(w.chars[1]) && is_ascii64(w.chars[2]);
      break;
    default:
      break;
  }
  if (!plausible) return;
  ph.synced = true;
  ph.since_word = 0;
  ph.misses = 0;
  w.acquired = true;
  offer(w, score, n);
}

void Demodulator::offer(const Word& w, int score, std::int64_t n) {
  if (pending_.valid && n - pending_index_ <= samples_per_symbol_) {
    if (score < pending_.score) {
      pending_.word = w;
      pending_.score = score;
      pending_index_ = n;
    }
    return;
  }
  flush_candidate(n, true);
  pending_.word = w;
  pending_.score = score;
  pending_.valid = true;
  pending_index_ = n;
}

void Demodulator::flush_candidate(std::int64_t n, bool force) {
  if (!pending_.valid) return;
  if (!force && n - pending_index_ <= samples_per_symbol_ + phase_step_) return;
  pending_.valid = false;
  // A different phase re-reporting the same word a symbol or so later, or
  // anything inside half a word of the last report, is not a new word.
  const std::int64_t half_word = static_cast<std::int64_t>(samples_per_symbol_) * kSymbolsPerWord / 2;
  if (pending_index_ - last_emit_index_ < half_word) return;
  last_emit_index_ = pending_index_;
  last_emit_raw_ = pending_.word.raw;
  ++words_;
  if (sink_) sink_(pending_.word);
}

// ---- Assembly --------------------------------------------------------------

const char* Transmission::kind_name(Kind k) {
  switch (k) {
    case Kind::kSounding: return "sounding";
    case Kind::kCall: return "call";
    case Kind::kCallPartial: return "call (caller not heard)";
    case Kind::kOther: return "unrecognised";
  }
  return "?";
}

std::string Transmission::summary() const {
  std::string to_list;
  for (const auto& t : to) to_list += (to_list.empty() ? "" : ", ") + t;
  std::string s;
  switch (kind) {
    case Kind::kSounding:
      s = "Sounding from " + from + (tws ? " (TWS: not accepting calls)" : " (TIS)");
      break;
    case Kind::kCall:
      s = from + " calling " + to_list + (tws ? " (TWS)" : "");
      break;
    case Kind::kCallPartial:
      s = "Call to " + to_list + " (calling station not heard)";
      break;
    case Kind::kOther:
      s = "ALE words without a recognisable call";
      break;
  }
  if (!amd.empty()) s += " · AMD: " + amd;
  for (const auto& c : other_commands) s += " · " + c;
  return s;
}

Assembler::Assembler(Sink sink, std::int64_t gap_ms) : sink_(std::move(sink)), gap_ms_(gap_ms) {}

void Assembler::add(const Word& w) {
  if (!words_.empty() && (w.time_ms - last_ms_ > gap_ms_ || w.channel_hz != words_.front().channel_hz)) finish();
  words_.push_back(w);
  last_ms_ = w.time_ms;
}

void Assembler::tick(std::int64_t now_ms) {
  if (!words_.empty() && now_ms - last_ms_ > gap_ms_) finish();
}

void Assembler::flush() {
  if (!words_.empty()) finish();
}

void Assembler::finish() {
  struct Section {
    int preamble;
    std::string chars;
  };
  std::vector<Section> sections;
  for (const auto& w : words_) {
    const bool starts = w.preamble != kData && w.preamble != kRep;
    if (starts || sections.empty()) sections.push_back({starts ? w.preamble : kData, ""});
    sections.back().chars.append(w.chars, 3);
  }

  Transmission t;
  t.start_ms = words_.front().time_ms - static_cast<std::int64_t>(kWordSeconds * 1000.0);
  t.end_ms = words_.back().time_ms;
  t.channel_hz = words_.front().channel_hz;
  t.words = static_cast<int>(words_.size());
  for (const auto& w : words_) t.max_errors = std::max(t.max_errors, w.errors);

  std::string from_fallback;
  for (const auto& s : sections) {
    const std::string addr = strip_fill(s.chars);
    switch (s.preamble) {
      case kTo:
        if (std::find(t.to.begin(), t.to.end(), addr) == t.to.end()) t.to.push_back(addr);
        break;
      case kTis:
      case kTws:
        t.from = addr;
        t.tws = s.preamble == kTws;
        break;
      case kFrom:
        from_fallback = addr;
        break;
      case kCmd: {
        const bool amd = std::all_of(s.chars.begin(), s.chars.end(), [](char c) { return is_ascii64(c); });
        if (amd) {
          std::string text = s.chars;
          while (!text.empty() && text.back() == ' ') text.pop_back();
          if (t.amd.find(text) == std::string::npos) t.amd += (t.amd.empty() ? "" : " ") + text;
        } else {
          char hex[8];
          std::string raw;
          for (char c : s.chars) {
            std::snprintf(hex, sizeof hex, "%02X", static_cast<unsigned>(c) & 0x7F);
            raw += hex;
          }
          const std::string label = "CMD not decoded (0x" + raw + ")";
          if (std::find(t.other_commands.begin(), t.other_commands.end(), label) == t.other_commands.end())
            t.other_commands.push_back(label);
        }
        break;
      }
      default:
        break;  // THRU, orphan DATA/REP: kept in the raw word log only
    }
  }
  if (t.from.empty()) t.from = from_fallback;

  if (!t.to.empty() && !t.from.empty()) t.kind = Transmission::Kind::kCall;
  else if (!t.to.empty()) t.kind = Transmission::Kind::kCallPartial;
  else if (!t.from.empty()) t.kind = Transmission::Kind::kSounding;
  else t.kind = Transmission::Kind::kOther;

  words_.clear();
  if (sink_) sink_(t);
}

std::vector<std::uint32_t> address_words(int preamble, const std::string& address) {
  std::string a = address;
  while (a.empty() || a.size() % 3 != 0) a.push_back('@');
  std::vector<std::uint32_t> out;
  for (std::size_t i = 0; i < a.size(); i += 3) {
    const std::size_t n = i / 3;
    const int p = n == 0 ? preamble : (n % 2 == 1 ? kData : kRep);
    out.push_back(make_word(p, a[i], a[i + 1], a[i + 2]));
  }
  return out;
}

std::vector<float> synthesize_audio(const std::vector<std::uint32_t>& words, double rate, double amplitude,
                                    double lead_in_s, double tone_offset_hz) {
  const auto sps = static_cast<std::size_t>(std::lround(rate * kSymbolSeconds));
  std::vector<float> out(static_cast<std::size_t>(lead_in_s * rate), 0.0f);
  double phase = 0.0;
  for (std::uint32_t w : words) {
    for (std::uint8_t tone : word_symbols(w)) {
      const double f = kFirstToneHz + kToneStepHz * tone + tone_offset_hz;
      for (std::size_t i = 0; i < sps; ++i) {
        phase = std::remainder(phase + kTwoPi * f / rate, kTwoPi);
        out.push_back(static_cast<float>(amplitude * std::sin(phase)));
      }
    }
  }
  return out;
}

}  // namespace ghostnet::rx::ale
