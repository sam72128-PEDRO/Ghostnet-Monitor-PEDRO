#pragma once

// 2G ALE (MIL-STD-188-141 Appendix A) receive: 8-FSK demodulation, word
// sync, Golay (24,12) decoding and calling-protocol assembly. Receive only:
// nothing here builds or sends a response, sounding or acknowledgement.
//
// On-air format, as implemented (cross-checked against LinuxALE by Charles
// Brain and Ilkka Toivanen, 2001, the original PC-ALE author's open
// receiver; no code is copied from it):
//   - 8 tones, 750-2500 Hz in 250 Hz steps, 8 ms symbols (125 baud),
//     3 bits per symbol, Gray coded (tone k carries k ^ (k >> 1)), MSB first.
//   - A 24-bit word = 3-bit preamble + three 7-bit characters. It is split
//     into two 12-bit halves, each extended-Golay encoded (g = 0xAE3, data
//     then 11 check bits then overall parity); the second codeword's 12 check
//     bits are inverted. The two codewords are bit-interleaved (A, B, A, B,
//     ... MSB first), a stuff bit is appended (49 bits), and the 49 bits are
//     sent three times: 147 bits = 49 symbols = 392 ms per word.
//   - The receiver majority-votes the three copies, then Golay-corrects up
//     to 3 bit errors per half.

#include <array>
#include <complex>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ghostnet::rx::ale {

inline constexpr double kSymbolSeconds = 0.008;
inline constexpr int kSymbolsPerWord = 49;
inline constexpr double kWordSeconds = kSymbolSeconds * kSymbolsPerWord;  // 0.392
inline constexpr int kTones = 8;
inline constexpr double kFirstToneHz = 750.0;
inline constexpr double kToneStepHz = 250.0;

enum Preamble : int {
  kData = 0,
  kThru = 1,
  kTo = 2,
  kTws = 3,  // "this was": conclusion / sounding, not accepting calls
  kFrom = 4,
  kTis = 5,  // "this is": conclusion / sounding, accepting calls
  kCmd = 6,
  kRep = 7,
};

const char* preamble_name(int preamble);

// ---- Golay (24,12) ---------------------------------------------------------

// Codeword layout: data in bits 23..12, check bits in 11..0.
std::uint32_t golay_encode(std::uint16_t data12);
// Returns the corrected 12 data bits; *errors is 0-3 bits corrected, or 4 if
// the codeword is uncorrectable (data returned uncorrected).
std::uint16_t golay_decode(std::uint32_t codeword, int* errors);

// ---- Word codec ------------------------------------------------------------

std::uint32_t make_word(int preamble, char a, char b, char c);
// The 147 transmitted bits (oldest first) for one word.
std::array<std::uint8_t, 3 * kSymbolsPerWord> word_bits(std::uint32_t word);
// The 49 transmitted symbols (tone indices 0..7) for one word.
std::array<std::uint8_t, kSymbolsPerWord> word_symbols(std::uint32_t word);

struct WordDecode {
  std::uint32_t word = 0;
  int errors_a = 4, errors_b = 4;  // Golay corrections per half; 4 = failed
  int bad_votes = 48;              // of 48 bits, how many the 3 copies disagreed on
  bool correctable() const { return errors_a <= 3 && errors_b <= 3; }
  int errors() const { return errors_a > errors_b ? errors_a : errors_b; }
};
// `bits` are the 147 received bits, oldest first.
WordDecode decode_bits(const std::uint8_t* bits);

// Character sets. Addresses use the 38-character basic set (A-Z, 0-9, @, ?);
// AMD text uses the 64-character set (0x20-0x5F).
bool is_basic38(char c);
bool is_ascii64(char c);

struct Word {
  std::uint32_t raw = 0;
  int preamble = 0;
  char chars[4] = {0, 0, 0, 0};
  int errors = 0;     // worst Golay correction count of the two halves
  int bad_votes = 0;  // of 48
  std::int64_t time_ms = 0;  // end of the word, receiver audio clock
  std::uint32_t channel_hz = 0;
  bool acquired = false;  // true: started a sync (strict test); false: continuation of a synced stream
  std::string text() const;  // e.g. "TO GHO"
};

// ---- Demodulator -----------------------------------------------------------

// 8-FSK demodulator and word synchroniser on USB audio. Runs eight symbol-
// timing phases in parallel (1 ms apart) with independent word sync, and
// arbitrates between them so each word is reported once.
//
// A word is reported when either:
//   - acquisition: both Golay halves need at most 1 correction, the copies
//     mostly agree, and the word is a plausible address word (TO, TIS, TWS,
//     THRU or FROM with basic-38 characters, or CMD with 64-set characters).
//     The odds of noise passing this are around 1e-9 per test; or
//   - continuation: exactly one word after a reported word on the same
//     timing phase, and both halves are correctable. Two failures in a row
//     drop sync.
class Demodulator {
 public:
  using WordSink = std::function<void(const Word&)>;

  explicit Demodulator(WordSink sink, double sample_rate_hz = 12000.0);

  // `start_ms` is the receiver audio-clock time of the first sample.
  void process(const float* audio, std::size_t count, std::int64_t start_ms);

  // Discard all state (after a retune). Stream time keeps running. A word
  // still awaiting arbitration is delivered first.
  void reset();

  // Deliver a word still awaiting arbitration (end of stream).
  void flush() { flush_candidate(sample_index_, true); }

  void set_channel(std::uint32_t channel_hz) { channel_hz_ = channel_hz; }

  // True while any timing phase holds word sync.
  bool in_sync() const;
  std::uint64_t words() const { return words_; }
  // Tone-energy share of the strongest tone in recent symbols: ~1/8 on
  // noise, near 1 on a clean ALE signal.
  float tone_quality() const { return quality_; }

 private:
  struct Phase {
    std::array<std::uint8_t, kSymbolsPerWord> symbols{};
    int count = 0;    // symbols received (saturates)
    int head = 0;     // ring position of the next symbol
    bool synced = false;
    int since_word = 0;  // symbols since the last reported word
    int misses = 0;
  };
  struct Candidate {
    Word word;
    int score = 0;  // lower is better
    bool valid = false;
  };

  void on_symbol(Phase& ph, int symbol, std::int64_t sample_index);
  void offer(const Word& w, int score, std::int64_t sample_index);
  void flush_candidate(std::int64_t sample_index, bool force);
  std::int64_t sample_ms(std::int64_t sample_index) const;

  WordSink sink_;
  double rate_;
  int samples_per_symbol_;
  int phase_step_;
  std::array<std::complex<double>, kTones> step_{};
  std::array<std::complex<double>, kTones> osc_{};
  std::vector<std::array<std::complex<float>, kTones>> hist_;
  std::array<std::complex<double>, kTones> sum_{};
  std::size_t hist_pos_ = 0;
  std::int64_t sample_index_ = 0;  // since construction
  std::int64_t first_index_ = 0;   // sample index of the last process() call's first sample
  std::int64_t first_ms_ = 0;
  std::int64_t reset_index_ = 0;
  std::vector<Phase> phases_;
  Candidate pending_;
  std::int64_t pending_index_ = 0;
  std::int64_t last_emit_index_ = -1000000;
  std::uint32_t last_emit_raw_ = 0;
  std::uint32_t channel_hz_ = 0;
  std::uint64_t words_ = 0;
  float quality_ = 0.0f;
  int renorm_ = 0;
};

// ---- Protocol assembly -----------------------------------------------------

// One station's transmission (a run of words with no gap longer than about
// three words), interpreted per the 2G calling protocol.
struct Transmission {
  enum class Kind {
    kSounding,     // TIS/TWS with no TO: a station announcing itself
    kCall,         // TO ... TIS/TWS: a call from one station to another
    kCallPartial,  // TO heard, caller's TIS/TWS not heard
    kOther,        // words heard, no recognisable call structure
  };
  Kind kind = Kind::kOther;
  std::int64_t start_ms = 0;
  std::int64_t end_ms = 0;
  std::uint32_t channel_hz = 0;
  std::vector<std::string> to;   // called addresses, first-heard order, unique
  std::string from;              // TIS/TWS address (the transmitting station)
  bool tws = false;              // conclusion was TWS ("not accepting calls" / link end)
  std::string amd;               // AMD (automatic message display) text
  std::vector<std::string> other_commands;  // CMD sections not decoded (raw)
  int words = 0;
  int max_errors = 0;
  std::string summary() const;
  static const char* kind_name(Kind k);
};

class Assembler {
 public:
  using Sink = std::function<void(const Transmission&)>;
  explicit Assembler(Sink sink, std::int64_t gap_ms = 1300);

  void add(const Word& w);
  // Closes the current transmission if nothing has arrived for gap_ms.
  void tick(std::int64_t now_ms);
  void flush();
  bool active() const { return !words_.empty(); }
  std::int64_t last_word_ms() const { return last_ms_; }

 private:
  void finish();

  Sink sink_;
  std::int64_t gap_ms_;
  std::vector<Word> words_;
  std::int64_t last_ms_ = 0;
};

// Builds the word sequence for an address section: first word with
// `preamble`, continuation words alternating DATA, REP; padded with '@'.
std::vector<std::uint32_t> address_words(int preamble, const std::string& address);

// Test helper: renders words as USB audio (phase-continuous 8-FSK).
std::vector<float> synthesize_audio(const std::vector<std::uint32_t>& words, double sample_rate_hz = 12000.0,
                                    double amplitude = 0.3, double lead_in_s = 0.0, double tone_offset_hz = 0.0);

}  // namespace ghostnet::rx::ale
