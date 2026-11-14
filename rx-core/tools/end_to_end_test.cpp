// End-to-end synthetic test of the whole receive chain, in the exact data
// format an RTL-SDR delivers:
//
//   typed GhostNet message -> js8core message builder -> JS8 frames
//   -> 8-FSK tones -> USB signal placed in a 240 kS/s complex capture
//      (dongle tuned 25 kHz above the dial), plus Gaussian noise, a strong
//      off-channel carrier and a DC offset -> u8 IQ
//   -> Receiver (SSB demod, AGC, js8core engine, frame unpack, assembly)
//   -> assembled message text
//
// The capture is replayed at ~10x real time with sample-clock timestamps
// anchored to a past UTC slot boundary, so the decoder's slot alignment is
// exercised the same way it is live.
//
// The JS8 *encoder* is used here only to generate the fixture. It is not
// linked into the Android app.

#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "ghostnet/rx/receiver.hpp"
#include "ghostnet/rx/test_signal.hpp"
#include "js8core/decoder.hpp"
#include "js8core/protocol/constants.hpp"
#include "js8core/protocol/costas.hpp"
#include "js8core/protocol/varicode.hpp"

using namespace ghostnet::rx;
namespace vc = js8core::protocol::varicode;

namespace {

constexpr double kFs = 240000.0;
constexpr int kPeriodMs = 15000;

int failures = 0;
void check(bool ok, const std::string& what) {
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
  if (!ok) ++failures;
}

struct Scenario {
  std::string name;
  double signal_counts;  // tone amplitude in u8 counts
  double noise_counts;   // per-axis Gaussian sigma in u8 counts
  bool mirrored = false;    // deliver a spectrum-inverted capture
  bool rx_invert = false;   // receiver invert_spectrum setting
  bool expect_decode = true;
  double audio_hz = 1500.0;
};

TestSignal synthesize(const Scenario& s, const std::string& mycall, const std::string& text) {
  auto built = vc::build_message_frames(mycall, "EL96", "", text, false, false, 0, nullptr);
  std::vector<Js8Tones> tones(built.size());
  for (std::size_t f = 0; f < built.size(); ++f) {
    js8core::legacy_encode(built[f].second, js8core::protocol::costas(js8core::protocol::CostasType::Original),
                           built[f].first.c_str(), tones[f].data());
  }
  TestSignalParams p;
  p.audio_hz = s.audio_hz;
  p.signal_counts = s.signal_counts;
  p.noise_counts = s.noise_counts;
  p.mirrored = s.mirrored;
  p.sample_rate_hz = static_cast<std::uint32_t>(kFs);
  p.capture_offset_hz = 25000;
  return synthesize_test_signal(tones, p);
}

double js8_snr_db(const Scenario& s) {
  // JS8 reports SNR in a 2500 Hz reference bandwidth.
  const double signal = s.signal_counts * s.signal_counts;
  const double noise = 2.0 * s.noise_counts * s.noise_counts * 2500.0 / kFs;
  return 10.0 * std::log10(signal / noise);
}

void run(const Scenario& s) {
  const std::string mycall = "KN4CRD";
  const std::string typed = "@GHOSTNET TEST MESSAGE FROM FLORIDA STATION 1";
  const std::string expected = "KN4CRD: @GHOSTNET TEST MESSAGE FROM FLORIDA STATION 1";

  std::printf("\n== %s (synthetic SNR %.1f dB in 2.5 kHz) ==\n", s.name.c_str(), js8_snr_db(s));
  const TestSignal cap = synthesize(s, mycall, typed);

  std::mutex m;
  std::vector<FrameEvent> frames;
  std::vector<AssembledMessage> messages;
  ReceiverCallbacks cb;
  cb.on_frame = [&](const FrameEvent& f) {
    std::lock_guard<std::mutex> l(m);
    frames.push_back(f);
  };
  cb.on_message = [&](const AssembledMessage& msg) {
    std::lock_guard<std::mutex> l(m);
    messages.push_back(msg);
  };

  ReceiverConfig cfg;
  cfg.dial_hz = 7107000;
  cfg.sample_rate_hz = static_cast<std::uint32_t>(kFs);
  cfg.capture_offset_hz = 25000;
  cfg.invert_spectrum = s.rx_invert;
  cfg.sample_clock_timestamps = true;
  cfg.replay_start_unix_ms = cap.start_unix_ms;

  Receiver rx(cfg, cb);
  if (!rx.start(nullptr)) {
    check(false, "receiver starts");
    return;
  }

  // Replay at ~10x real time in USB-sized chunks.
  const std::size_t chunk = 16384;
  const double speed = 10.0;
  const auto t0 = std::chrono::steady_clock::now();
  for (std::size_t off = 0; off < cap.iq.size(); off += chunk) {
    const std::size_t n = std::min(chunk, cap.iq.size() - off);
    rx.push_iq(cap.iq.data() + off, n);
    const double audio_s = static_cast<double>(off + n) / 2.0 / kFs;
    std::this_thread::sleep_until(t0 + std::chrono::duration<double>(audio_s / speed));
  }
  rx.wait_idle();
  std::this_thread::sleep_for(std::chrono::seconds(2));  // let the last decode pass finish
  const ReceiverStats st = rx.stats();
  rx.stop();

  std::lock_guard<std::mutex> l(m);
  std::printf("     decode passes %llu, frames %zu, messages %zu, dropped bytes %llu, IQ level %.1f dBFS, "
              "median dt %.2f s\n",
              static_cast<unsigned long long>(st.decode_passes), frames.size(), messages.size(),
              static_cast<unsigned long long>(st.iq_bytes_dropped), st.iq_level_dbfs, st.median_dt_s);
  for (const auto& f : frames) {
    std::printf("     frame slot+%lld s  %-12s flags=%d snr=%d off=%.1f  %s\n",
                static_cast<long long>((f.utc_ms - cap.first_slot_unix_ms) / 1000), f.frame.raw.c_str(),
                f.frame.flags, f.snr_db, f.offset_hz, f.frame.text.c_str());
  }
  for (const auto& msg : messages) {
    std::printf("     message complete=%d gap=%d missing_start=%d: '%s'\n", msg.complete, msg.has_gap,
                msg.missing_start, msg.text.c_str());
  }

  check(st.iq_bytes_dropped == 0, s.name + ": no IQ dropped at 10x real time");
  if (!s.expect_decode) {
    check(frames.empty(), s.name + ": no frames decoded (expected none)");
    return;
  }
  check(frames.size() == cap.frames, s.name + ": every frame decoded exactly once (" + std::to_string(frames.size()) +
                                         "/" + std::to_string(cap.frames) + ")");
  bool slots_ok = frames.size() == cap.frames;
  for (std::size_t i = 0; slots_ok && i < frames.size(); ++i) {
    slots_ok = frames[i].utc_ms == cap.first_slot_unix_ms + static_cast<std::int64_t>(i) * kPeriodMs;
  }
  check(slots_ok, s.name + ": frames attributed to the correct UTC transmit slots");
  bool offset_ok = !frames.empty();
  for (const auto& f : frames) offset_ok = offset_ok && std::abs(f.offset_hz - s.audio_hz) < 5.0;
  check(offset_ok, s.name + ": audio offset reported within 5 Hz of " + std::to_string(int(s.audio_hz)) + " Hz");
  const bool one = messages.size() == 1;
  check(one && messages[0].complete && messages[0].text == expected && messages[0].to == "@GHOSTNET" &&
            messages[0].from == mycall,
        s.name + ": assembled complete message '" + expected + "'");
}

}  // namespace

int main() {
  run({"clean signal", 3.0, 4.0});
  run({"weak signal", 0.8, 10.0, false, false, true, 1100.0});
  run({"inverted capture, invert on", 3.0, 4.0, true, true});
  run({"inverted capture, invert off", 3.0, 4.0, true, false, false});

  {
    std::printf("\n== embedded on-device self-test ==\n");
    const auto r = run_self_test(10.0);
    std::printf("     %s | decoded '%s' in %.1f s wall for %.0f s of capture\n", r.detail.c_str(),
                r.decoded_text.c_str(), r.wall_seconds, r.capture_seconds);
    check(r.passed, "self-test decodes '" + r.expected_text + "'");
  }

  if (failures) {
    std::fprintf(stderr, "\n%d check(s) failed\n", failures);
    return EXIT_FAILURE;
  }
  std::printf("\nall checks passed\n");
  return EXIT_SUCCESS;
}
