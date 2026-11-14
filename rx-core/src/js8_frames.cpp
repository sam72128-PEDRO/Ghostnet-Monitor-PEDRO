#include "ghostnet/rx/js8_frames.hpp"

#include <array>
#include <cctype>
#include <vector>

#include "js8core/protocol/varicode.hpp"

namespace ghostnet::rx {

namespace vc = js8core::protocol::varicode;

namespace {

// JS8 protocol data: the CQ variants selected by a heartbeat frame's
// 3-bit sub-type when its "alt" flag is set.
constexpr std::array<const char*, 8> kCqStrings = {"CQ CQ CQ", "CQ DX", "CQ QRP", "CQ CONTEST",
                                                   "CQ FIELD", "CQ FD", "CQ CQ",  "CQ"};

std::string trim(const std::string& s) {
  std::size_t b = 0;
  std::size_t e = s.size();
  while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
  return s.substr(b, e - b);
}

void append_word(std::string* out, const std::string& word) {
  const std::string w = trim(word);
  if (w.empty()) return;
  if (!out->empty() && out->back() != ' ') out->push_back(' ');
  *out += w;
}

bool looks_like_grid(const std::string& s) {
  const std::string t = trim(s);
  return (t.size() == 4 || t.size() == 6) && std::isalpha(static_cast<unsigned char>(t[0])) &&
         std::isalpha(static_cast<unsigned char>(t[1])) && std::isdigit(static_cast<unsigned char>(t[2])) &&
         std::isdigit(static_cast<unsigned char>(t[3]));
}

bool try_fast_data(UnpackedFrame* f) {
  if ((f->flags & kFrameFastData) == 0) return false;
  f->text = vc::unpack_fast_data_message(f->raw);
  if (f->text.empty()) return false;
  f->kind = FrameKind::kFastData;
  return true;
}

bool try_data(UnpackedFrame* f) {
  if ((f->flags & kFrameFastData) != 0) return false;
  f->text = vc::unpack_data_message(f->raw);
  if (f->text.empty()) return false;
  f->kind = FrameKind::kData;
  return true;
}

bool try_heartbeat(UnpackedFrame* f) {
  std::uint8_t type = 0;
  std::uint8_t bits3 = 0;
  bool alt = false;
  const auto parts = vc::unpack_heartbeat_message(f->raw, &type, &alt, &bits3);
  if (parts.empty() || trim(parts[0]).empty()) return false;
  f->from = trim(parts[0]);
  if (parts.size() > 2 && looks_like_grid(parts[2])) f->grid = trim(parts[2]);
  std::string text = f->from + ":";
  if (alt) {
    f->kind = FrameKind::kCq;
    f->to = "@ALLCALL";
    f->command = kCqStrings[bits3 & 7];
  } else {
    f->kind = FrameKind::kHeartbeat;
    f->to = "@HB";
    f->command = "HEARTBEAT";
  }
  append_word(&text, f->to);
  append_word(&text, f->command);
  append_word(&text, f->grid);
  f->text = text;
  return true;
}

bool try_compound(UnpackedFrame* f) {
  std::uint8_t type = 0;
  std::uint8_t bits3 = 0;
  std::uint16_t num = 0;
  const auto parts = vc::unpack_compound_message(f->raw, &type, &num, &bits3);
  if (parts.empty() || trim(parts[0]).empty()) return false;
  const std::string call = trim(parts[0]);
  std::string extra;
  for (std::size_t i = 2; i < parts.size(); ++i) extra += parts[i];

  // The First frame of a message always identifies the sender. A station
  // that has a grid configured sends it as a compound frame (type 1, with
  // the grid); without a grid the same identification goes out as a
  // compound-directed frame (type 2). Only later type-2 frames name the
  // destination.
  if (type == 2 && !f->first()) {
    f->kind = FrameKind::kCompoundDirected;
    f->to = call;
    f->command = trim(extra);
    std::string text = f->to;
    append_word(&text, f->command);
    f->text = text;
    return true;
  }

  f->kind = FrameKind::kCompound;
  f->from = call;
  std::string text = call + ":";
  if (looks_like_grid(extra)) {
    f->grid = trim(extra);
    // The grid on a message's header frame is station metadata, not part
    // of what the operator typed; keep it out of the assembled text.
    if (!(f->first() && !f->last())) append_word(&text, f->grid);
  } else {
    f->command = trim(extra);
    append_word(&text, f->command);
  }
  f->text = text;
  return true;
}

bool try_directed(UnpackedFrame* f) {
  std::uint8_t type = 0;
  const auto parts = vc::unpack_directed_message(f->raw, &type);
  if (parts.size() < 2) return false;
  f->kind = FrameKind::kDirected;
  f->from = trim(parts[0]);
  f->to = trim(parts[1]);
  std::string text = f->from + ":";
  append_word(&text, f->to);
  if (parts.size() > 2) {
    f->command = trim(parts[2]);
    append_word(&text, f->command);
  }
  for (std::size_t i = 3; i < parts.size(); ++i) append_word(&text, parts[i]);
  f->text = text;
  return true;
}

}  // namespace

const char* frame_kind_name(FrameKind kind) {
  switch (kind) {
    case FrameKind::kHeartbeat: return "heartbeat";
    case FrameKind::kCq: return "cq";
    case FrameKind::kCompound: return "compound";
    case FrameKind::kCompoundDirected: return "compound_directed";
    case FrameKind::kDirected: return "directed";
    case FrameKind::kData: return "data";
    case FrameKind::kFastData: return "fast_data";
    case FrameKind::kUnknown: return "unknown";
  }
  return "unknown";
}

bool is_group(const std::string& callsign_or_group) {
  return !callsign_or_group.empty() && callsign_or_group[0] == '@';
}

UnpackedFrame unpack_frame(const std::string& raw, int flags) {
  UnpackedFrame f;
  f.raw = raw;
  f.flags = flags;
  try {
    // Order matters: data frames are recognised by their leading flag bit,
    // and the fast-data unpacker happily returns garbage for any frame that
    // was not sent as fast data, so it is gated on the transmission flag.
    if (try_fast_data(&f) || try_data(&f) || try_heartbeat(&f) || try_compound(&f) || try_directed(&f)) return f;
  } catch (...) {
    // Corrupt-but-CRC-valid frames should be vanishingly rare; never let one
    // take down the receive pipeline.
  }
  UnpackedFrame unknown;
  unknown.raw = raw;
  unknown.flags = flags;
  return unknown;
}

}  // namespace ghostnet::rx
