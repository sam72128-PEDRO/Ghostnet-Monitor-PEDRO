#pragma once

#include <cstdint>
#include <string>

namespace ghostnet::rx {

// Transmission flags carried alongside each decoded JS8 frame (the
// decoder's `type` field): first frame of a message, last frame, and
// "fast data" encoding.
inline constexpr int kFrameFirst = 1;
inline constexpr int kFrameLast = 2;
inline constexpr int kFrameFastData = 4;

enum class FrameKind {
  kHeartbeat,         // "@HB HEARTBEAT <grid>"
  kCq,                // "@ALLCALL CQ ..."
  kCompound,          // sender identification: callsign (+ grid or command)
  kCompoundDirected,  // destination: callsign or @GROUP (+ command)
  kDirected,          // from + to + command in one frame
  kData,              // free text continuation (Huffman / JSC)
  kFastData,          // free text continuation, fast-data encoding
  kUnknown,           // decoded by the modem but not unpackable here
};

const char* frame_kind_name(FrameKind kind);

// One decoded frame turned into text plus whatever addressing it carries.
// `raw` is the exact 12-character modem output and is always preserved;
// everything else is derived from it and may be empty.
struct UnpackedFrame {
  std::string raw;
  int flags = 0;
  FrameKind kind = FrameKind::kUnknown;
  std::string text;
  std::string from;
  std::string to;  // callsign or @GROUP
  std::string grid;
  std::string command;  // e.g. "SNR?", trimmed; empty for free text

  bool first() const { return (flags & kFrameFirst) != 0; }
  bool last() const { return (flags & kFrameLast) != 0; }
  // A frame's text is a continuation of free text: concatenate without a
  // separator (data frames carry their own word spacing).
  bool is_text_continuation() const { return kind == FrameKind::kData || kind == FrameKind::kFastData; }
};

// Pure function of (raw frame, flags). Never throws; an unrecognised frame
// comes back as kUnknown with an empty `text` and the raw string intact.
UnpackedFrame unpack_frame(const std::string& raw, int flags);

bool is_group(const std::string& callsign_or_group);

}  // namespace ghostnet::rx
