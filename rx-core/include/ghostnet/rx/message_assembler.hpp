#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "ghostnet/rx/js8_frames.hpp"

namespace ghostnet::rx {

// JS8 submode IDs as reported by js8core (events::Decoded::mode).
inline constexpr int kSubmodeNormal = 0;  // A, 15 s
inline constexpr int kSubmodeFast = 1;    // B, 10 s
inline constexpr int kSubmodeTurbo = 2;   // C, 6 s
inline constexpr int kSubmodeSlow = 3;    // E, 30 s
inline constexpr int kSubmodeUltra = 4;   // I, 4 s

int submode_period_ms(int submode);
const char* submode_name(int submode);

// js8core reports events::Decoded::mode as 0/1/2/4/8 for A/B/C/E/I (not the
// SubmodeId index 0..4). Converts that to the kSubmode* constants above;
// returns -1 for anything unrecognised.
int submode_from_event_mode(int event_mode);

// Milliseconds from slot start until a frame of this submode is fully
// received (frame duration + start delay). A decode cannot arrive earlier.
int submode_frame_end_ms(int submode);

// A single decoded frame plus reception metadata.
struct FrameEvent {
  std::int64_t utc_ms = 0;  // start of the transmit slot the frame was sent in
  double offset_hz = 0.0;   // audio offset above the dial
  int snr_db = 0;
  int submode = kSubmodeNormal;
  float dt_s = 0.0f;
  float quality = 0.0f;
  UnpackedFrame frame;
};

struct AssembledMessage {
  std::vector<FrameEvent> frames;
  std::string text;
  std::string from;
  std::string to;  // callsign or @GROUP; empty for undirected text
  std::string grid;
  int submode = kSubmodeNormal;
  double offset_hz = 0.0;
  std::int64_t first_utc_ms = 0;
  std::int64_t last_utc_ms = 0;
  int snr_min_db = 0;
  int snr_max_db = 0;
  // complete: saw both the First and Last frame with nothing outstanding.
  // missing_start: the first frame we saw was not flagged First.
  // timed_out: closed because no further frame arrived in time.
  // has_gap: a transmit period was skipped between two frames, i.e. at
  // least one frame in the middle was not decoded.
  bool complete = false;
  bool missing_start = false;
  bool timed_out = false;
  bool has_gap = false;
};

// Rebuilds multi-frame JS8 messages from individual frames.
//
// JS8 sends one frame per transmit period on a fixed audio offset; the first
// carries the First flag and the last the Last flag. Frames are grouped by
// submode and offset (within `offset_tolerance_hz`). A message is emitted
// when its Last frame arrives, when a new First frame appears on the same
// channel, when no frame arrives for `timeout_periods` transmit periods, or
// on flush(). Every frame is kept; nothing is dropped for being partial.
class MessageAssembler {
 public:
  using MessageSink = std::function<void(const AssembledMessage&)>;

  explicit MessageAssembler(MessageSink sink, double offset_tolerance_hz = 10.0, double timeout_periods = 2.5);

  // Returns false (and ignores the frame) when it duplicates a frame already
  // seen on the same channel in the same slot — overlapping decode windows
  // can report one transmission twice.
  bool on_frame(const FrameEvent& frame);

  // Closes channels that have been idle too long. Call periodically.
  void tick(std::int64_t now_utc_ms);

  // Closes everything (end of a session).
  void flush();

  std::size_t open_channels() const { return open_.size(); }

 private:
  struct Channel {
    AssembledMessage msg;
    std::int64_t last_seen_ms = 0;
  };

  void close(std::size_t index, bool timed_out);
  Channel* find(const FrameEvent& frame, std::size_t* index);

  MessageSink sink_;
  double tolerance_hz_;
  double timeout_periods_;
  std::vector<Channel> open_;

  struct Seen {
    std::string raw;
    int submode;
    double offset_hz;
    std::int64_t utc_ms;
  };
  std::vector<Seen> recent_;
};

std::string assemble_text(const std::vector<FrameEvent>& frames);

}  // namespace ghostnet::rx
