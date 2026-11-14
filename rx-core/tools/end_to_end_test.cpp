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

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <map>
#include <mutex>
#include <random>
#include <cmath>
#include <string>
#include <thread>
#include <vector>

#include "ghostnet/rx/file_iq_source.hpp"
#include "ghostnet/rx/receiver.hpp"
#include "ghostnet/rx/rtty_test_signal.hpp"
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

// GHOSTNET_TEST_SPEED overrides the replay speed, e.g. to run the arm64
// build under QEMU, which emulates too slowly for the default 10x.
double replay_speed() {
  const char* v = std::getenv("GHOSTNET_TEST_SPEED");
  return v ? std::atof(v) : 10.0;
}

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
  std::string record_path;
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
  // Waterfall: keep the per-bin maximum over the whole run.
  std::vector<float> peak_row;
  std::size_t rows = 0;
  float row_bin_hz = 0.0f;
  cb.on_spectrum = [&](const std::vector<float>& row, float bin_hz) {
    std::lock_guard<std::mutex> l(m);
    if (peak_row.size() != row.size()) peak_row.assign(row.size(), -400.0f);
    for (std::size_t i = 0; i < row.size(); ++i) peak_row[i] = std::max(peak_row[i], row[i]);
    row_bin_hz = bin_hz;
    ++rows;
  };

  ReceiverConfig cfg;
  cfg.dial_hz = 7107000;
  cfg.sample_rate_hz = static_cast<std::uint32_t>(kFs);
  cfg.capture_offset_hz = 25000;
  cfg.invert_spectrum = s.rx_invert;
  cfg.sample_clock_timestamps = true;
  cfg.replay_start_unix_ms = cap.start_unix_ms;
  cfg.record_path = s.record_path;

  Receiver rx(cfg, cb);
  if (!rx.start(nullptr)) {
    check(false, "receiver starts");
    return;
  }

  // Replay at ~10x real time in USB-sized chunks.
  const std::size_t chunk = 16384;
  const double speed = replay_speed();
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

  check(st.iq_bytes_dropped == 0, s.name + ": no IQ dropped at replay speed");
  if (!s.record_path.empty()) {
    std::FILE* f = std::fopen(s.record_path.c_str(), "rb");
    long size = -1;
    if (f) {
      std::fseek(f, 0, SEEK_END);
      size = std::ftell(f);
      std::fclose(f);
    }
    check(size == static_cast<long>(cap.iq.size()), s.name + ": recording holds every IQ byte (" +
                                                        std::to_string(size) + "/" + std::to_string(cap.iq.size()) + ")");
    check(read_capture_meta(s.record_path).count("start_unix_ms") == 1, s.name + ": recording has .meta with start time");
  }
  if (s.expect_decode) {
    // The signal sits at audio_hz..audio_hz+44 Hz; compare that band's peak
    // with the median of the whole row.
    std::vector<float> sorted = peak_row;
    std::sort(sorted.begin(), sorted.end());
    const float median = sorted.empty() ? 0.0f : sorted[sorted.size() / 2];
    float in_band = -400.0f;
    for (std::size_t i = 0; i < peak_row.size(); ++i) {
      const double hz = i * row_bin_hz;
      if (hz >= s.audio_hz - 10 && hz <= s.audio_hz + 50) in_band = std::max(in_band, peak_row[i]);
    }
    std::printf("     waterfall: %zu rows, signal band peak %.1f dB vs median %.1f dB\n", rows, in_band, median);
    check(rows > 100 && in_band - median > 10.0f, s.name + ": waterfall shows the signal at its audio offset");
  }
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

// A recording made by the receiver must replay through a fresh receiver and
// decode the same message: this is how field captures get debugged.
void replay_recording(const std::string& path) {
  std::printf("\n== replay of the receiver's own recording ==\n");
  const auto meta = read_capture_meta(path);
  ReceiverConfig cfg;
  cfg.dial_hz = static_cast<std::uint32_t>(std::stoul(meta.at("dial_hz")));
  cfg.capture_offset_hz = std::stoi(meta.at("capture_offset_hz"));
  cfg.sample_rate_hz = static_cast<std::uint32_t>(std::stoul(meta.at("sample_rate_hz")));
  cfg.sample_clock_timestamps = true;
  cfg.replay_start_unix_ms = std::stoll(meta.at("start_unix_ms"));
  std::mutex m;
  std::vector<AssembledMessage> messages;
  ReceiverCallbacks cb;
  cb.on_message = [&](const AssembledMessage& msg) {
    std::lock_guard<std::mutex> l(m);
    messages.push_back(msg);
  };
  FileIqSource file(path, cfg.sample_rate_hz, replay_speed());
  Receiver rx(cfg, cb);
  rx.start(&file);
  file.wait_finished();
  rx.wait_idle();
  std::this_thread::sleep_for(std::chrono::seconds(2));
  rx.stop();
  std::lock_guard<std::mutex> l(m);
  const bool ok = messages.size() == 1 && messages[0].complete &&
                  messages[0].text == "KN4CRD: @GHOSTNET TEST MESSAGE FROM FLORIDA STATION 1";
  std::printf("     %s\n", messages.empty() ? "(no messages)" : messages[0].text.c_str());
  check(ok, "recording replays to the same complete message");
  std::remove(path.c_str());
  std::remove((path + ".meta").c_str());
}

// ---- other receive modes through the full Receiver ---------------------

constexpr double kTwoPi = 6.28318530717958647692;

double goertzel_magnitude(const std::vector<float>& x, double hz, double rate) {
  const double w = kTwoPi * hz / rate, c = 2.0 * std::cos(w);
  double s1 = 0, s2 = 0;
  for (float v : x) {
    const double s0 = v + c * s1 - s2;
    s2 = s1;
    s1 = s0;
  }
  const double re = s1 - s2 * std::cos(w), im = s2 * std::sin(w);
  return std::sqrt(re * re + im * im);
}

// Audio-rate frequency track (Hz above/below the dial; negative = below)
// rendered as RTL-SDR u8 IQ at 240 kS/s with the dongle 25 kHz above the dial.
std::vector<std::uint8_t> iq_from_audio_track(const std::vector<float>& audio_freq, double audio_rate, double noise,
                                              double amp = 3.0, bool carrier = true) {
  const auto up = static_cast<std::size_t>(kFs / audio_rate);
  std::mt19937 rng(11);
  std::normal_distribution<double> n(0.0, noise);
  std::vector<std::uint8_t> iq(2 * audio_freq.size() * up);
  double phase = 0.0;
  for (std::size_t i = 0; i < audio_freq.size() * up; ++i) {
    phase = std::remainder(phase + kTwoPi * (audio_freq[i / up] - 25000.0) / kFs, kTwoPi);
    const double a = carrier && audio_freq[i / up] != 0.0f ? amp : 0.0;
    iq[2 * i] = static_cast<std::uint8_t>(std::clamp(std::lround(127.5 + a * std::cos(phase) + n(rng)), 0L, 255L));
    iq[2 * i + 1] = static_cast<std::uint8_t>(std::clamp(std::lround(127.5 + a * std::sin(phase) + n(rng)), 0L, 255L));
  }
  return iq;
}

void push_all(Receiver& rx, const std::vector<std::uint8_t>& iq) {
  const auto t0 = std::chrono::steady_clock::now();
  for (std::size_t off = 0; off < iq.size(); off += 16384) {
    const std::size_t n = std::min<std::size_t>(16384, iq.size() - off);
    rx.push_iq(iq.data() + off, n);
    std::this_thread::sleep_until(t0 + std::chrono::duration<double>((off + n) / 2.0 / kFs / replay_speed()));
  }
  rx.wait_idle();
}

void rtty_through_receiver() {
  std::printf("\n== RTTY 45.45 baud through the full receiver (7.077 MHz USB) ==\n");
  const std::string text = "RYRYRYRY GHOSTNET RTTY NET CHECK IN DE N0CALL 73\n";
  RttyTestParams p;
  p.center_hz = 1830.0;
  const auto iq = iq_from_audio_track(rtty_frequency_track(text, p), 12000.0, 3.0);
  std::mutex m;
  std::vector<RttyLine> lines;
  ReceiverCallbacks cb;
  cb.on_rtty_line = [&](const RttyLine& l) {
    std::lock_guard<std::mutex> g(m);
    lines.push_back(l);
  };
  ReceiverConfig cfg;
  cfg.mode = ReceiveMode::kRtty;
  cfg.dial_hz = 7077000;
  cfg.record_path = "ghostnet_rtty_rec.cu8";
  Receiver rx(cfg, cb);
  rx.start(nullptr);
  push_all(rx, iq);
  const auto st = rx.stats();
  rx.stop();
  std::lock_guard<std::mutex> g(m);
  std::printf("     centre %.1f Hz locked=%d quality %.2f, %zu line(s): '%s'\n", st.rtty_center_hz, st.rtty_locked,
              st.rtty_quality, lines.size(), lines.empty() ? "" : lines[0].text.c_str());
  check(!lines.empty() && lines[0].text.find("GHOSTNET RTTY NET CHECK IN DE N0CALL 73") != std::string::npos,
        "RTTY text decoded through the full IQ chain");
  check(std::abs(st.rtty_center_hz - 1830.0) < 10.0, "RTTY auto-centre at 1830 Hz");
  check(st.recorded_bytes == iq.size(), "digital mode (RTTY) honours IQ recording");
  std::remove(cfg.record_path.c_str());
  std::remove((cfg.record_path + ".meta").c_str());
}

double voice_tone(ReceiveMode mode, bool fm, bool carrier, double tone_hz, double* rms_out, bool* squelch_open,
                  const std::string& record_path = "") {
  // 4 s of signal: FM deviation 2.5 kHz around the channel, or an SSB tone.
  const double audio_rate = 12000.0;
  const auto n = static_cast<std::size_t>(4.0 * audio_rate);
  std::vector<float> f(n);
  for (std::size_t i = 0; i < n; ++i) {
    const double t = i / audio_rate;
    f[i] = fm ? static_cast<float>(0.001 + 2500.0 * std::sin(kTwoPi * tone_hz * t))  // never exactly 0
              : static_cast<float>(-tone_hz);  // LSB: tone below the dial
  }
  const auto iq = iq_from_audio_track(f, audio_rate, fm ? 6.0 : 3.0, fm ? 20.0 : 3.0, carrier);
  std::mutex m;
  std::vector<float> audio;
  ReceiverCallbacks cb;
  cb.on_audio = [&](const std::int16_t* p, std::size_t k) {
    std::lock_guard<std::mutex> g(m);
    for (std::size_t i = 0; i < k; ++i) audio.push_back(p[i] / 32768.0f);
  };
  ReceiverConfig cfg;
  cfg.mode = mode;
  cfg.sideband = ghostnet::rf::Sideband::kLsb;
  cfg.record_path = record_path;
  Receiver rx(cfg, cb);
  rx.start(nullptr);
  push_all(rx, iq);
  const auto st = rx.stats();
  rx.stop();
  if (squelch_open) *squelch_open = st.squelch_open;
  std::lock_guard<std::mutex> g(m);
  std::vector<float> steady(audio.begin() + std::min<std::size_t>(audio.size(), 12000), audio.end());
  double ss = 0;
  for (float v : steady) ss += double(v) * v;
  *rms_out = steady.empty() ? 0 : std::sqrt(ss / steady.size());
  return steady.empty() ? 0 : goertzel_magnitude(steady, tone_hz, 12000.0) * 2.0 / steady.size();
}

void voice_through_receiver() {
  std::printf("\n== analog voice through the full receiver ==\n");
  double rms = 0;
  bool open = false;
  const double fm_tone = voice_tone(ReceiveMode::kVoiceFm, true, true, 1000.0, &rms, &open);
  std::printf("     FM carrier: 1 kHz tone %.3f of audio RMS %.3f, squelch open=%d\n", fm_tone / std::sqrt(2.0), rms, open);
  check(open && fm_tone / std::sqrt(2.0) > 0.7 * rms, "FM voice: 1 kHz tone dominates the audio, squelch open");

  double noise_rms = 1;
  bool noise_open = true;
  voice_tone(ReceiveMode::kVoiceFm, true, false, 1000.0, &noise_rms, &noise_open);
  std::printf("     FM no carrier: audio RMS %.4f, squelch open=%d\n", noise_rms, noise_open);
  check(!noise_open && noise_rms < 0.01, "FM voice: squelch mutes pure noise");

  const double ssb_tone = voice_tone(ReceiveMode::kVoiceSsb, false, true, 1000.0, &rms, nullptr, "ghostnet_voice_rec.cu8");
  std::printf("     LSB: 1 kHz tone %.3f of audio RMS %.3f\n", ssb_tone / std::sqrt(2.0), rms);
  check(ssb_tone / std::sqrt(2.0) > 0.7 * rms, "LSB voice: tone 1 kHz below the dial comes out as 1 kHz audio");
  std::FILE* f = std::fopen("ghostnet_voice_rec.cu8", "rb");
  check(f == nullptr, "voice mode never writes an IQ recording, even if asked");
  if (f) std::fclose(f);
}

// A simulated dongle for scanning tests: every channel has its own audio-
// frequency track running on a shared clock (stations keep transmitting
// while the receiver is elsewhere). Retunes take effect one USB buffer late,
// as on hardware, and data is delivered at `speed` x real time.
class ScanTestSource : public IqSource {
 public:
  ScanTestSource(std::map<std::uint32_t, std::vector<float>> tracks, std::uint32_t center, double seconds, double speed)
      : tracks_(std::move(tracks)), center_(center), seconds_(seconds), speed_(speed) {}
  ~ScanTestSource() override { stop(); }

  bool start(DataFn on_data, ErrorFn) override {
    running_ = true;
    thread_ = std::thread([this, on_data] {
      std::mt19937 rng(21);
      std::normal_distribution<double> n(0.0, 3.0);
      const std::size_t block = 8192;  // IQ pairs per buffer
      const auto total = static_cast<std::size_t>(seconds_ * kFs);
      std::vector<std::uint8_t> buf(2 * block);
      double phase = 0.0;
      std::uint32_t center = center_;
      const auto t0 = std::chrono::steady_clock::now();
      for (std::size_t s0 = 0; s0 < total && running_; s0 += block) {
        const std::uint32_t requested = center_.load();
        for (std::size_t i = 0; i < block; ++i) {
          const std::size_t s = s0 + i;
          float f = 0.0f;
          auto it = tracks_.find(center - 25000);
          if (it != tracks_.end() && s / 20 < it->second.size()) f = it->second[s / 20];
          phase = std::remainder(phase + kTwoPi * (f - 25000.0) / kFs, kTwoPi);
          const double a = f != 0.0f ? 3.0 : 0.0;
          buf[2 * i] = static_cast<std::uint8_t>(std::clamp(std::lround(127.5 + a * std::cos(phase) + n(rng)), 0L, 255L));
          buf[2 * i + 1] = static_cast<std::uint8_t>(std::clamp(std::lround(127.5 + a * std::sin(phase) + n(rng)), 0L, 255L));
        }
        center = requested;  // a retune lands one buffer late
        on_data(buf.data(), buf.size());
        std::this_thread::sleep_until(t0 + std::chrono::duration<double>((s0 + block) / kFs / speed_));
      }
      done_ = true;
    });
    return true;
  }
  void stop() override {
    running_ = false;
    if (thread_.joinable()) thread_.join();
  }
  bool retune(std::uint32_t center_hz) override {
    center_ = center_hz;
    ++retunes_;
    return true;
  }
  std::string description() const override { return "scan test source"; }
  bool done() const { return done_; }
  int retunes() const { return retunes_; }

 private:
  std::map<std::uint32_t, std::vector<float>> tracks_;
  std::atomic<std::uint32_t> center_;
  double seconds_, speed_;
  std::atomic<bool> running_{false}, done_{false};
  std::atomic<int> retunes_{0};
  std::thread thread_;
};

// ALE words as a 12 kHz audio-frequency track (0 = silence), starting at t0.
void put_ale(std::vector<float>& track, double t0, const std::vector<std::uint32_t>& words) {
  std::size_t i = static_cast<std::size_t>(t0 * 12000.0);
  for (auto w : words) {
    for (auto tone : ale::word_symbols(w)) {
      for (int k = 0; k < 96; ++k, ++i) {
        if (i >= track.size()) return;
        track[i] = static_cast<float>(ale::kFirstToneHz + ale::kToneStepHz * tone);
      }
    }
  }
}

void ale_scanning_through_receiver() {
  std::printf("\n== 2G ALE scanning through the full receiver (simulated dongle with retune) ==\n");
  const double secs = 32.0;
  const std::vector<std::uint32_t> channels = {3598500, 7104500, 10128500, 14111500};
  std::map<std::uint32_t, std::vector<float>> tracks;
  for (auto c : channels) tracks[c].assign(static_cast<std::size_t>(secs * 12000.0), 0.0f);

  // 10.1285 MHz: a scanning sounding (TWS KX4ABC, 12 repeats ~9.4 s) at t=2 s.
  std::vector<std::uint32_t> snd;
  for (int i = 0; i < 12; ++i) {
    auto w = ale::address_words(ale::kTws, "KX4ABC");
    snd.insert(snd.end(), w.begin(), w.end());
  }
  put_ale(tracks[10128500], 2.0, snd);
  // 7.1045 MHz: scanning call TO GHOSTNET (8 repeats) + AMD + TIS N0CALL at t=14 s.
  std::vector<std::uint32_t> call;
  for (int i = 0; i < 8; ++i) {
    auto w = ale::address_words(ale::kTo, "GHOSTNET");
    call.insert(call.end(), w.begin(), w.end());
  }
  auto lead = ale::address_words(ale::kTo, "GHOSTNET");
  call.insert(call.end(), lead.begin(), lead.end());
  call.push_back(ale::make_word(ale::kCmd, 'Q', 'R', 'V'));
  auto tis = ale::address_words(ale::kTis, "N0CALL");
  call.insert(call.end(), tis.begin(), tis.end());
  put_ale(tracks[7104500], 14.0, call);

  std::mutex m;
  std::vector<ale::Transmission> tx;
  std::vector<ale::Word> words;
  ReceiverCallbacks cb;
  cb.on_ale_word = [&](const ale::Word& w) {
    std::lock_guard<std::mutex> g(m);
    words.push_back(w);
  };
  cb.on_ale_transmission = [&](const ale::Transmission& t) {
    std::lock_guard<std::mutex> g(m);
    tx.push_back(t);
  };
  ReceiverConfig cfg;
  cfg.mode = ReceiveMode::kAle;
  cfg.ale.channels_hz = channels;
  cfg.ale.dwell_ms = 1000;
  cfg.ale.hold_ms = 4000;
  cfg.sample_clock_timestamps = true;
  cfg.replay_start_unix_ms = 1'700'000'000'000;
  cfg.record_path = "ghostnet_ale_scan.cu8";
  ReceiverConfig c0 = cfg;
  c0.dial_hz = channels[0];
  ScanTestSource src(tracks, capture_center_hz(c0), secs, replay_speed());
  Receiver rx(cfg, cb);
  rx.start(&src);
  while (!src.done()) std::this_thread::sleep_for(std::chrono::milliseconds(20));
  rx.wait_idle();
  const auto st = rx.stats();
  rx.stop();

  std::lock_guard<std::mutex> g(m);
  for (const auto& t : tx) {
    std::printf("     %.3f MHz +%5.1f s  %-10s %s (%d words)\n", t.channel_hz / 1e6,
                (t.start_ms - cfg.replay_start_unix_ms) / 1000.0, ale::Transmission::kind_name(t.kind),
                t.summary().c_str(), t.words);
  }
  std::printf("     %d retunes, %zu words\n", src.retunes(), words.size());
  bool sounding = false, called = false, wrong_channel = false;
  for (const auto& t : tx) {
    if (t.kind == ale::Transmission::Kind::kSounding && t.from == "KX4ABC" && t.tws && t.channel_hz == 10128500)
      sounding = true;
    if (t.kind == ale::Transmission::Kind::kCall && t.from == "N0CALL" && t.channel_hz == 7104500 && !t.to.empty() &&
        t.to[0] == "GHOSTNET" && t.amd == "QRV")
      called = true;
    if ((t.from == "KX4ABC" && t.channel_hz != 10128500) || (t.from == "N0CALL" && t.channel_hz != 7104500))
      wrong_channel = true;
  }
  check(sounding, "scan stops on 10.1285 MHz and logs the TWS sounding from KX4ABC");
  check(called, "scan stops on 7.1045 MHz and logs N0CALL calling GHOSTNET with AMD 'QRV'");
  check(!wrong_channel, "every transmission is attributed to the channel it was on");
  check(st.retunes >= 8 && st.retune_failures == 0, "receiver kept scanning between them (" + std::to_string(st.retunes) + " retunes)");
  check(!st.recording && st.recorded_bytes == 0, "no IQ recording while scanning");
  std::remove(cfg.record_path.c_str());
  std::remove((cfg.record_path + ".meta").c_str());
}

}  // namespace

int main() {
  const std::string rec = "ghostnet_e2e_recording.cu8";
  Scenario clean{"clean signal", 3.0, 4.0};
  clean.record_path = rec;
  run(clean);
  replay_recording(rec);
  run({"weak signal", 0.8, 10.0, false, false, true, 1100.0});
  run({"inverted capture, invert on", 3.0, 4.0, true, true});
  run({"inverted capture, invert off", 3.0, 4.0, true, false, false});

  rtty_through_receiver();
  voice_through_receiver();
  ale_scanning_through_receiver();

  {
    std::printf("\n== embedded on-device self-test ==\n");
    const auto r = run_self_test(replay_speed());
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
