#include "ghostnet/rx/rtty.hpp"

#include <algorithm>
#include <cmath>

#include "ghostnet/rx/spectrum.hpp"

namespace ghostnet::rx {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr std::size_t kSearchFft = 2048;
constexpr int kSearchFramesPerDecision = 12;  // ~2 s at 12 kHz
constexpr std::int64_t kLineIdleMs = 5000;
constexpr std::size_t kMaxLine = 200;
constexpr float kMinQuality = 0.55f;

// ITA2 letters and US-TTY figures, indexed by 5-bit code. '\0' = no output.
constexpr char kLetters[32] = {'\0', 'E', '\n', 'A', ' ', 'S', 'I', 'U', '\r', 'D', 'R', 'J', 'N', 'F', 'C', 'K',
                               'T',  'Z', 'L',  'W', 'H', 'Y', 'P', 'Q', 'O',  'B', 'G', '\0', 'M', 'X', 'V', '\0'};
constexpr char kFigures[32] = {'\0', '3', '\n', '-', ' ', '\a', '8', '7', '\r', '$', '4', '\'', ',', '!', ':', '(',
                               '5',  '"', ')',  '2', '#', '6',  '0', '1', '9',  '?', '&', '\0', '.', '/', ';', '\0'};
constexpr int kFigs = 0x1B;
constexpr int kLtrs = 0x1F;
}  // namespace

char RttyDecoder::decode_baudot(int code, bool figures) { return (figures ? kFigures : kLetters)[code & 31]; }

RttyDecoder::RttyDecoder(RttyConfig config, CharSink on_char, LineSink on_line)
    : config_(config),
      on_char_(std::move(on_char)),
      on_line_(std::move(on_line)),
      samples_per_bit_(config.sample_rate_hz / config.baud),
      search_power_(kSearchFft / 2, 0.0) {
  const auto n = static_cast<std::size_t>(std::lround(samples_per_bit_));
  mark_hist_.assign(n, {0, 0});
  space_hist_.assign(n, {0, 0});
  if (config_.center_hz > 0) {
    set_center(config_.center_hz);
    locked_ = true;
  } else {
    set_center(1500.0);
  }
  search_buf_.reserve(kSearchFft);
}

void RttyDecoder::set_center(double hz) {
  center_ = hz;
  const double hi = hz + config_.shift_hz / 2.0;
  const double lo = hz - config_.shift_hz / 2.0;
  mark_hz_ = config_.reverse ? lo : hi;
  space_hz_ = config_.reverse ? hi : lo;
  auto step = [&](double f) {
    const double w = 2.0 * kPi * f / config_.sample_rate_hz;
    return std::complex<double>(std::cos(w), -std::sin(w));
  };
  mark_step_ = step(mark_hz_);
  space_step_ = step(space_hz_);
  std::fill(mark_hist_.begin(), mark_hist_.end(), std::complex<float>{0, 0});
  std::fill(space_hist_.begin(), space_hist_.end(), std::complex<float>{0, 0});
  mark_sum_ = space_sum_ = {0, 0};
  mark_peak_ = space_peak_ = 1e-6f;
  state_ = State::kIdle;
  good_since_lock_ = bad_since_lock_ = 0;
}

// Averaged spectrum over ~2 s; lock onto the strongest pair of peaks exactly
// one shift apart, if it stands well above the noise.
void RttyDecoder::search_center() {
  std::vector<std::complex<float>> buf(kSearchFft);
  for (std::size_t i = 0; i < kSearchFft; ++i) {
    const double w = 0.5 - 0.5 * std::cos(2.0 * kPi * i / (kSearchFft - 1));
    buf[i] = {static_cast<float>(search_buf_[i] * w), 0.0f};
  }
  fft_radix2(buf);
  for (std::size_t k = 0; k < kSearchFft / 2; ++k) search_power_[k] += std::norm(buf[k]);
  // Decide quickly (~1 s) while searching, more carefully once locked.
  if (++search_frames_ < (locked_ ? kSearchFramesPerDecision : kSearchFramesPerDecision / 2)) return;

  const double bin_hz = config_.sample_rate_hz / kSearchFft;
  auto power_near = [&](double hz) {
    const auto c = static_cast<long>(std::lround(hz / bin_hz));
    double p = 0.0;
    for (long k = c - 2; k <= c + 2; ++k) {
      if (k >= 0 && k < static_cast<long>(search_power_.size())) p = std::max(p, search_power_[static_cast<std::size_t>(k)]);
    }
    return p;
  };
  std::vector<double> sorted(search_power_.begin() + static_cast<long>(300 / bin_hz),
                             search_power_.begin() + static_cast<long>(2800 / bin_hz));
  std::nth_element(sorted.begin(), sorted.begin() + static_cast<long>(sorted.size() / 2), sorted.end());
  const double floor = std::max(sorted[sorted.size() / 2], 1e-30);

  double best_score = 0.0;
  double best_center = 0.0;
  for (double c = 300.0 + config_.shift_hz / 2; c <= 2700.0 - config_.shift_hz / 2; c += bin_hz / 2) {
    const double score = std::min(power_near(c - config_.shift_hz / 2), power_near(c + config_.shift_hz / 2));
    if (score > best_score) {
      best_score = score;
      best_center = c;
    }
  }
  // The tone peaks are flat-topped at this resolution, so refine the centre
  // from the power-weighted centroid of each tone.
  auto centroid = [&](double hz) {
    const auto c = static_cast<long>(std::lround(hz / bin_hz));
    double sw = 0.0, s = 0.0;
    for (long k = c - 4; k <= c + 4; ++k) {
      if (k < 0 || k >= static_cast<long>(search_power_.size())) continue;
      const double w = search_power_[static_cast<std::size_t>(k)];
      sw += w;
      s += w * k * bin_hz;
    }
    return sw > 0 ? s / sw : hz;
  };
  if (best_score > 0) {
    best_center = (centroid(best_center - config_.shift_hz / 2) + centroid(best_center + config_.shift_hz / 2)) / 2.0;
  }
  std::fill(search_power_.begin(), search_power_.end(), 0.0);
  search_frames_ = 0;

  if (best_score > floor * 30.0) {  // both tones ~15 dB above the noise median
    if (!locked_ || std::abs(best_center - center_) > 12.0) set_center(best_center);
    locked_ = true;
  } else if (locked_ && bad_since_lock_ > good_since_lock_) {
    locked_ = false;  // signal gone and recent decoding was mostly errors
  }
}

void RttyDecoder::process(const float* audio, std::size_t count, std::int64_t audio_time_ms) {
  const double ms_per_sample = 1000.0 / config_.sample_rate_hz;
  const std::size_t n = mark_hist_.size();
  for (std::size_t i = 0; i < count; ++i) {
    const float x = audio[i];
    now_ms_ = audio_time_ms + static_cast<std::int64_t>(i * ms_per_sample);

    if (config_.center_hz <= 0) {
      search_buf_.push_back(x);
      if (search_buf_.size() == kSearchFft) {
        search_center();
        search_buf_.erase(search_buf_.begin(), search_buf_.begin() + kSearchFft / 2);
      }
    }

    // Matched filters: mix each tone to DC and integrate over one bit.
    const auto m = std::complex<float>(static_cast<float>(x * mark_phasor_.real()), static_cast<float>(x * mark_phasor_.imag()));
    const auto s = std::complex<float>(static_cast<float>(x * space_phasor_.real()), static_cast<float>(x * space_phasor_.imag()));
    mark_phasor_ *= mark_step_;
    space_phasor_ *= space_step_;
    mark_sum_ += m - mark_hist_[hist_pos_];
    space_sum_ += s - space_hist_[hist_pos_];
    mark_hist_[hist_pos_] = m;
    space_hist_[hist_pos_] = s;
    hist_pos_ = (hist_pos_ + 1) % n;
    if (hist_pos_ == 0) {
      mark_phasor_ /= std::abs(mark_phasor_);
      space_phasor_ /= std::abs(space_phasor_);
    }

    // Per-tone level tracking so selective fading doesn't bias decisions.
    const float ma = std::abs(mark_sum_);
    const float sa = std::abs(space_sum_);
    mark_peak_ = std::max(ma, mark_peak_ * 0.99995f);
    space_peak_ = std::max(sa, space_peak_ * 0.99995f);
    const float d = ma / mark_peak_ - sa / space_peak_;
    quality_ += 0.0005f * (std::abs(d) - quality_);
    on_bit_sample(d, now_ms_);
    previous_d_ = d;
    sample_index_ += 1.0;
  }
  if (!line_.text.empty() && now_ms_ - last_char_ms_ > kLineIdleMs) end_line(now_ms_);
}

void RttyDecoder::on_bit_sample(float d, std::int64_t now_ms) {
  if (!locked_) {
    state_ = State::kIdle;
    return;
  }
  switch (state_) {
    case State::kIdle:
      // Mark-to-space transition: the boxcar output crosses zero half a bit
      // into the start bit, so the first full-integration point (end of the
      // start bit) is half a bit later, then one bit apart from there on.
      if (previous_d_ > 0.0f && d <= 0.0f) {
        state_ = State::kStart;
        next_sample_at_ = sample_index_ + samples_per_bit_ / 2.0;
      }
      break;
    case State::kStart:
      if (sample_index_ >= next_sample_at_) {
        if (d < 0.0f) {
          state_ = State::kData;
          bit_count_ = 0;
          code_ = 0;
          next_sample_at_ += samples_per_bit_;
        } else {
          state_ = State::kIdle;  // glitch, not a start bit
        }
      }
      break;
    case State::kData:
      if (sample_index_ >= next_sample_at_) {
        if (d > 0.0f) code_ |= 1 << bit_count_;  // mark = 1, LSB first
        ++bit_count_;
        next_sample_at_ += samples_per_bit_;
        if (bit_count_ == 5) state_ = State::kStop;
      }
      break;
    case State::kStop:
      if (sample_index_ >= next_sample_at_) {
        if (quality_ > kMinQuality) emit_char(code_, d <= 0.0f, now_ms);
        state_ = State::kIdle;
      }
      break;
  }
}

void RttyDecoder::emit_char(int code, bool framing_error, std::int64_t now_ms) {
  if (framing_error) {
    ++errors_;
    ++bad_since_lock_;
  } else {
    ++good_since_lock_;
  }
  std::string out;
  if (framing_error) {
    out = "~";
  } else if (code == kFigs) {
    figures_ = true;
  } else if (code == kLtrs) {
    figures_ = false;
  } else {
    const char c = decode_baudot(code, figures_);
    if (c == ' ' && config_.unshift_on_space) figures_ = false;
    if (c == '\a') out = "[BELL]";
    else if (c != '\0') out = std::string(1, c);
  }
  if (out.empty()) return;
  ++chars_;
  if (on_char_) on_char_(out, framing_error);

  if (out == "\r" || out == "\n") {
    if (!line_.text.empty()) end_line(now_ms);
    return;
  }
  if (line_.text.empty()) {
    line_.start_ms = now_ms;
    line_.center_hz = center_;
  }
  line_.text += out;
  ++line_.characters;
  if (framing_error) ++line_.framing_errors;
  line_.end_ms = now_ms;
  last_char_ms_ = now_ms;
  if (line_.text.size() >= kMaxLine) end_line(now_ms);
}

void RttyDecoder::end_line(std::int64_t now_ms) {
  // Trim trailing spaces; a line of only spaces/errors isn't worth storing.
  while (!line_.text.empty() && line_.text.back() == ' ') line_.text.pop_back();
  const bool meaningful = std::any_of(line_.text.begin(), line_.text.end(), [](char c) { return c != ' ' && c != '~'; });
  if (meaningful && on_line_) {
    if (line_.end_ms == 0) line_.end_ms = now_ms;
    on_line_(line_);
  }
  line_ = RttyLine{};
}

void RttyDecoder::flush() {
  if (!line_.text.empty()) end_line(now_ms_);
}

}  // namespace ghostnet::rx
