#pragma once

#include <complex>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ghostnet::rx {

struct RttyConfig {
  double sample_rate_hz = 12000.0;
  double baud = 45.45;
  // GhostNet V1.5 gives the baud rate but not the shift; 170 Hz is the
  // amateur standard and the spec's provisional default.
  double shift_hz = 170.0;
  // Audio centre between mark and space. <= 0 means find it automatically
  // from the spectrum (the booklet doesn't say where in the passband).
  double center_hz = 0.0;
  // Amateur convention: mark is the higher RF frequency, which is the higher
  // audio tone in USB. Reverse swaps them.
  bool reverse = false;
  // Unshift-on-space: return to letters after a space, as most stations expect.
  bool unshift_on_space = true;
};

struct RttyLine {
  std::string text;           // '~' marks a character with a framing error
  std::int64_t start_ms = 0;  // timestamps in the receiver's audio clock
  std::int64_t end_ms = 0;
  int characters = 0;
  int framing_errors = 0;
  double center_hz = 0.0;
};

// 45.45 baud Baudot (US-TTY figures) RTTY receiver working on demodulated
// USB audio. Mark/space matched filters with per-tone level tracking (so a
// fade on one tone doesn't bias decisions), start-bit sync with mid-bit
// sampling, LTRS/FIGS shift state. A character whose stop bit isn't mark is
// reported as a framing error, never guessed.
class RttyDecoder {
 public:
  using CharSink = std::function<void(const std::string& text, bool framing_error)>;
  using LineSink = std::function<void(const RttyLine& line)>;

  RttyDecoder(RttyConfig config, CharSink on_char, LineSink on_line);

  // `audio_time_ms` is the time of the first sample (for line timestamps).
  void process(const float* audio, std::size_t count, std::int64_t audio_time_ms);

  // Ends the current line (session stop).
  void flush();

  double center_hz() const { return center_; }
  bool locked() const { return locked_; }
  std::uint64_t characters() const { return chars_; }
  std::uint64_t framing_errors() const { return errors_; }
  float quality() const { return quality_; }

  static char decode_baudot(int code, bool figures);

 private:
  void set_center(double hz);
  void search_center();
  void on_bit_sample(float d, std::int64_t now_ms);
  void emit_char(int code, bool framing_error, std::int64_t now_ms);
  void end_line(std::int64_t now_ms);

  RttyConfig config_;
  CharSink on_char_;
  LineSink on_line_;

  // Tone filters: complex mixers + boxcar integrators of one bit length.
  double center_ = 1500.0;
  bool locked_ = false;
  double mark_hz_ = 0.0;
  double space_hz_ = 0.0;
  std::complex<double> mark_phasor_{1, 0}, mark_step_{1, 0};
  std::complex<double> space_phasor_{1, 0}, space_step_{1, 0};
  std::vector<std::complex<float>> mark_hist_, space_hist_;
  std::complex<float> mark_sum_{0, 0}, space_sum_{0, 0};
  std::size_t hist_pos_ = 0;
  float mark_peak_ = 1e-6f, space_peak_ = 1e-6f;

  // UART state.
  double samples_per_bit_;
  enum class State { kIdle, kStart, kData, kStop } state_ = State::kIdle;
  double next_sample_at_ = 0.0;
  double sample_index_ = 0.0;
  int bit_count_ = 0;
  int code_ = 0;
  float previous_d_ = 0.0f;
  bool figures_ = false;

  // Auto-centre search from an averaged spectrum.
  std::vector<float> search_buf_;
  std::vector<double> search_power_;
  int search_frames_ = 0;

  // Line assembly.
  RttyLine line_;
  std::int64_t last_char_ms_ = 0;
  std::int64_t now_ms_ = 0;
  std::uint64_t chars_ = 0;
  std::uint64_t errors_ = 0;
  int good_since_lock_ = 0;
  int bad_since_lock_ = 0;
  // Smoothed |mark - space| decision strength: near 1 with a real signal,
  // much lower on noise. Characters are only emitted above a threshold, so
  // noise doesn't print garbage even with a fixed centre.
  float quality_ = 0.0f;
};

}  // namespace ghostnet::rx
