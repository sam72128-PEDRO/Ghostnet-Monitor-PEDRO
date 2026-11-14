// Frame unpacking + message assembly against frames produced by js8core's
// own message builder (the same code a transmitting JS8 station's software
// uses to split a typed message into frames). Covers GhostNet-style group
// messages, commands, CQ/heartbeat, plain text, and the failure cases a
// receiver actually sees: a lost first frame, a lost middle frame, a
// duplicate decode, an interrupted message, and two stations at once.

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "ghostnet/rx/js8_frames.hpp"
#include "ghostnet/rx/message_assembler.hpp"
#include "js8core/protocol/varicode.hpp"

using namespace ghostnet::rx;
namespace vc = js8core::protocol::varicode;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
  if (!ok) ++failures;
}

// Frames for `text` as sent by `mycall`, stamped one Normal period apart.
// The sender's grid changes how JS8 frames the sender identification, so
// tests run both with and without one.
std::string g_grid = "EL96";

std::vector<FrameEvent> frames_for(const std::string& mycall, const std::string& text, double offset_hz,
                                   std::int64_t t0_ms = 1'700'000'010'000) {
  std::vector<FrameEvent> out;
  auto built = vc::build_message_frames(mycall, g_grid, "", text, false, false, kSubmodeNormal, nullptr);
  std::int64_t t = t0_ms;
  for (auto& [raw, flags] : built) {
    FrameEvent fe;
    fe.utc_ms = t;
    fe.offset_hz = offset_hz;
    fe.snr_db = -10;
    fe.submode = kSubmodeNormal;
    fe.frame = unpack_frame(raw, flags);
    out.push_back(fe);
    t += submode_period_ms(kSubmodeNormal);
  }
  return out;
}

std::vector<AssembledMessage> run(const std::vector<FrameEvent>& frames, bool flush = true) {
  std::vector<AssembledMessage> got;
  MessageAssembler a([&](const AssembledMessage& m) { got.push_back(m); });
  for (const auto& f : frames) a.on_frame(f);
  if (flush) a.flush();
  return got;
}

void expect_message(const std::string& mycall, const std::string& typed, const std::string& expected_text,
                    const std::string& expected_to) {
  auto got = run(frames_for(mycall, typed, 1500.0));
  const bool one = got.size() == 1;
  check(one, "'" + typed + "' assembles to exactly one message");
  if (!one) return;
  const auto& m = got[0];
  check(m.text == expected_text, "'" + typed + "' text: got '" + m.text + "'");
  check(m.complete, "'" + typed + "' marked complete");
  check(m.to == expected_to, "'" + typed + "' destination: got '" + m.to + "'");
}

}  // namespace

int main() {
  for (const std::string grid : {"EL96", ""}) {
    g_grid = grid;
    std::printf("-- sender grid '%s' --\n", grid.c_str());
    expect_message("KN4CRD", "@GHOSTNET TEST MESSAGE FROM FLORIDA STATION 1",
                   "KN4CRD: @GHOSTNET TEST MESSAGE FROM FLORIDA STATION 1", "@GHOSTNET");
    expect_message("KN4CRD", "@GSTFLASH ALL STATIONS CHECK IN", "KN4CRD: @GSTFLASH ALL STATIONS CHECK IN",
                   "@GSTFLASH");
    expect_message("KN4CRD", "@GNUSAFL NET OPEN", "KN4CRD: @GNUSAFL NET OPEN", "@GNUSAFL");
    expect_message("KN4CRD", "@GHOSTNET SNR?", "KN4CRD: @GHOSTNET SNR?", "@GHOSTNET");
    expect_message("KN4CRD", "N0CALL HELLO THERE", "KN4CRD: N0CALL HELLO THERE", "N0CALL");
    expect_message("KN4CRD", "CQ CQ CQ", "KN4CRD: @ALLCALL CQ CQ CQ", "@ALLCALL");
  }
  g_grid = "EL96";
  expect_message("KN4CRD", "@HB HEARTBEAT EL96", "KN4CRD: @HB HEARTBEAT EL96", "@HB");

  {
    auto got = run(frames_for("KN4CRD", "PLAIN TEXT NO DIRECTION", 1500.0));
    check(got.size() == 1 && got[0].text == "PLAIN TEXT NO DIRECTION" && got[0].from.empty(),
          "undirected plain text keeps its text and has no sender");
  }

  {
    auto frames = frames_for("KN4CRD", "@GHOSTNET TEST MESSAGE FROM FLORIDA STATION 1", 1500.0);
    frames.erase(frames.begin());
    auto got = run(frames);
    check(got.size() == 1 && got[0].missing_start && !got[0].complete,
          "lost first frame -> kept, flagged missing_start, not complete");
  }

  {
    auto frames = frames_for("KN4CRD", "@GHOSTNET TEST MESSAGE FROM FLORIDA STATION 1", 1500.0);
    frames.erase(frames.begin() + 2);
    auto got = run(frames);
    check(got.size() == 1 && got[0].has_gap && !got[0].complete, "lost middle frame -> flagged has_gap, not complete");
  }

  {
    auto frames = frames_for("KN4CRD", "@GHOSTNET TEST MESSAGE FROM FLORIDA STATION 1", 1500.0);
    std::vector<AssembledMessage> got;
    MessageAssembler a([&](const AssembledMessage& m) { got.push_back(m); });
    int accepted = 0;
    for (std::size_t i = 0; i < frames.size(); ++i) {
      accepted += a.on_frame(frames[i]);
      if (i == 1) {
        auto dup = frames[i];
        dup.offset_hz += 2.0;  // same transmission, slightly different estimate
        accepted += a.on_frame(dup);
      }
    }
    check(accepted == static_cast<int>(frames.size()) && got.size() == 1 && got[0].complete,
          "duplicate decode of the same frame is dropped");
  }

  {
    auto frames = frames_for("KN4CRD", "@GHOSTNET TEST MESSAGE FROM FLORIDA STATION 1", 1500.0);
    std::vector<AssembledMessage> got;
    MessageAssembler a([&](const AssembledMessage& m) { got.push_back(m); });
    a.on_frame(frames[0]);
    a.on_frame(frames[1]);
    a.tick(frames[1].utc_ms + 30000);
    check(got.empty(), "message still open 2 periods after last frame");
    a.tick(frames[1].utc_ms + 45000);
    check(got.size() == 1 && got[0].timed_out && !got[0].complete, "message times out after 2.5 periods idle");
  }

  {
    auto a_frames = frames_for("KN4CRD", "@GHOSTNET STATION ALPHA REPORTING", 1200.0);
    auto b_frames = frames_for("N0CALL", "@GHOSTNET STATION BRAVO REPORTING IN", 1900.0);
    std::vector<FrameEvent> mixed;
    for (std::size_t i = 0; i < std::max(a_frames.size(), b_frames.size()); ++i) {
      if (i < a_frames.size()) mixed.push_back(a_frames[i]);
      if (i < b_frames.size()) mixed.push_back(b_frames[i]);
    }
    auto got = run(mixed);
    bool saw_a = false;
    bool saw_b = false;
    for (const auto& m : got) {
      saw_a |= m.text == "KN4CRD: @GHOSTNET STATION ALPHA REPORTING" && m.complete;
      saw_b |= m.text == "N0CALL: @GHOSTNET STATION BRAVO REPORTING IN" && m.complete;
    }
    check(got.size() == 2 && saw_a && saw_b, "two stations on different offsets assemble independently");
  }

  {
    auto first = frames_for("KN4CRD", "@GHOSTNET THIS ONE GETS CUT OFF EARLY", 1500.0);
    auto second = frames_for("KN4CRD", "@GHOSTNET SECOND", 1500.0, first[1].utc_ms + 15000);
    std::vector<FrameEvent> frames = {first[0], first[1]};
    frames.insert(frames.end(), second.begin(), second.end());
    auto got = run(frames);
    check(got.size() == 2 && !got[0].complete && got[1].complete && got[1].text == "KN4CRD: @GHOSTNET SECOND",
          "new First frame on the same offset closes the interrupted message");
  }

  {
    const auto u = unpack_frame("not-a-frame!", 0);
    check(u.kind == FrameKind::kUnknown && u.raw == "not-a-frame!", "garbage frame -> kUnknown, raw preserved");
  }

  if (failures) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return EXIT_FAILURE;
  }
  std::printf("all checks passed\n");
  return EXIT_SUCCESS;
}
