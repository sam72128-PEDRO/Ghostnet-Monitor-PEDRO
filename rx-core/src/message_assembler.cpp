#include "ghostnet/rx/message_assembler.hpp"

#include <algorithm>
#include <cmath>

namespace ghostnet::rx {

int submode_period_ms(int submode) {
  switch (submode) {
    case kSubmodeNormal: return 15000;
    case kSubmodeFast: return 10000;
    case kSubmodeTurbo: return 6000;
    case kSubmodeSlow: return 30000;
    case kSubmodeUltra: return 4000;
    default: return 15000;
  }
}

const char* submode_name(int submode) {
  switch (submode) {
    case kSubmodeNormal: return "Normal";
    case kSubmodeFast: return "Fast";
    case kSubmodeTurbo: return "Turbo";
    case kSubmodeSlow: return "Slow";
    case kSubmodeUltra: return "Ultra";
    default: return "Unknown";
  }
}

int submode_from_event_mode(int event_mode) {
  switch (event_mode) {
    case 0: return kSubmodeNormal;
    case 1: return kSubmodeFast;
    case 2: return kSubmodeTurbo;
    case 4: return kSubmodeSlow;
    case 8: return kSubmodeUltra;
    default: return -1;
  }
}

int submode_frame_end_ms(int submode) {
  // 79 symbols * samples-per-symbol / 12 kHz, plus the submode start delay.
  switch (submode) {
    case kSubmodeNormal: return 1920 * 79 / 12 + 500;
    case kSubmodeFast: return 1200 * 79 / 12 + 200;
    case kSubmodeTurbo: return 600 * 79 / 12 + 100;
    case kSubmodeSlow: return 3840 * 79 / 12 + 500;
    case kSubmodeUltra: return 384 * 79 / 12 + 100;
    default: return 13140;
  }
}

std::string assemble_text(const std::vector<FrameEvent>& frames) {
  std::string out;
  bool previous_was_continuation = false;
  for (const auto& fe : frames) {
    const auto& f = fe.frame;
    if (f.text.empty()) continue;
    const bool joined = f.is_text_continuation() && previous_was_continuation;
    if (!out.empty() && !joined && out.back() != ' ') out.push_back(' ');
    out += f.text;
    previous_was_continuation = f.is_text_continuation();
  }
  // Collapse runs of spaces and trim; data frames pad with spaces.
  std::string collapsed;
  for (char c : out) {
    if (c == ' ' && (collapsed.empty() || collapsed.back() == ' ')) continue;
    collapsed.push_back(c);
  }
  while (!collapsed.empty() && collapsed.back() == ' ') collapsed.pop_back();
  return collapsed;
}

MessageAssembler::MessageAssembler(MessageSink sink, double offset_tolerance_hz, double timeout_periods)
    : sink_(std::move(sink)), tolerance_hz_(offset_tolerance_hz), timeout_periods_(timeout_periods) {}

MessageAssembler::Channel* MessageAssembler::find(const FrameEvent& frame, std::size_t* index) {
  for (std::size_t i = 0; i < open_.size(); ++i) {
    const auto& m = open_[i].msg;
    if (m.submode == frame.submode && std::abs(m.offset_hz - frame.offset_hz) <= tolerance_hz_) {
      *index = i;
      return &open_[i];
    }
  }
  return nullptr;
}

bool MessageAssembler::on_frame(const FrameEvent& frame) {
  const int period = submode_period_ms(frame.submode);
  for (const auto& s : recent_) {
    if (s.raw == frame.frame.raw && s.submode == frame.submode &&
        std::abs(s.offset_hz - frame.offset_hz) <= tolerance_hz_ && std::llabs(s.utc_ms - frame.utc_ms) < period / 2) {
      return false;
    }
  }
  recent_.push_back({frame.frame.raw, frame.submode, frame.offset_hz, frame.utc_ms});
  const std::int64_t horizon = frame.utc_ms - 120000;
  recent_.erase(std::remove_if(recent_.begin(), recent_.end(), [&](const Seen& s) { return s.utc_ms < horizon; }),
                recent_.end());

  std::size_t index = 0;
  Channel* ch = find(frame, &index);
  if (ch && frame.frame.first()) {
    // A new message started on this offset before the old one finished.
    close(index, false);
    ch = nullptr;
  }
  if (!ch) {
    Channel fresh;
    fresh.msg.submode = frame.submode;
    fresh.msg.offset_hz = frame.offset_hz;
    fresh.msg.first_utc_ms = frame.utc_ms;
    fresh.msg.snr_min_db = frame.snr_db;
    fresh.msg.snr_max_db = frame.snr_db;
    fresh.msg.missing_start = !frame.frame.first();
    open_.push_back(std::move(fresh));
    index = open_.size() - 1;
    ch = &open_.back();
  }

  auto& m = ch->msg;
  // Multi-frame messages go out in consecutive periods; a longer gap means
  // a frame in between was never decoded.
  if (!m.frames.empty() && frame.utc_ms - m.last_utc_ms > period * 3 / 2) m.has_gap = true;
  m.frames.push_back(frame);
  m.last_utc_ms = frame.utc_ms;
  m.snr_min_db = std::min(m.snr_min_db, frame.snr_db);
  m.snr_max_db = std::max(m.snr_max_db, frame.snr_db);
  if (m.from.empty() && !frame.frame.from.empty()) m.from = frame.frame.from;
  if (m.to.empty() && !frame.frame.to.empty()) m.to = frame.frame.to;
  if (m.grid.empty() && !frame.frame.grid.empty()) m.grid = frame.frame.grid;
  ch->last_seen_ms = frame.utc_ms;

  if (frame.frame.last()) close(index, false);
  return true;
}

void MessageAssembler::close(std::size_t index, bool timed_out) {
  Channel ch = std::move(open_[index]);
  open_.erase(open_.begin() + static_cast<std::ptrdiff_t>(index));
  auto& m = ch.msg;
  m.timed_out = timed_out;
  m.complete = !m.missing_start && !m.has_gap && !timed_out && !m.frames.empty() && m.frames.back().frame.last();
  m.text = assemble_text(m.frames);
  if (sink_) sink_(m);
}

void MessageAssembler::tick(std::int64_t now_utc_ms) {
  for (std::size_t i = open_.size(); i-- > 0;) {
    const auto limit = static_cast<std::int64_t>(timeout_periods_ * submode_period_ms(open_[i].msg.submode));
    if (now_utc_ms - open_[i].last_seen_ms > limit) close(i, true);
  }
}

void MessageAssembler::flush() {
  while (!open_.empty()) close(open_.size() - 1, true);
}

}  // namespace ghostnet::rx
