// ghostnet-rx: desktop receive tool for the same pipeline the Android app
// uses. Lets the RF -> decode chain be tested with an RTL-SDR on a laptop
// before the tablet is involved, and records raw IQ for offline replay.
//
//   ghostnet-rx --list
//   ghostnet-rx --preset 40m                       live, 7.107 MHz
//   ghostnet-rx --freq 14107000 --record net.cu8   live + record IQ
//   ghostnet-rx --iq-file net.cu8                  replay a recording
//
// Receive-only. There is no transmit option because nothing in this
// codebase can transmit.

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "ghostnet/rx/file_iq_source.hpp"
#include "ghostnet/rx/receiver.hpp"
#include "ghostnet/rx/wav_audio.hpp"
#include "ghostnet/rx/rtlsdr_source.hpp"

using namespace ghostnet::rx;

namespace {

std::atomic<bool> g_quit{false};
std::mutex g_print;

void on_signal(int) { g_quit = true; }

std::int64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
      .count();
}

std::string utc_hms(std::int64_t unix_ms) {
  const std::time_t t = static_cast<std::time_t>(unix_ms / 1000);
  std::tm tm{};
  gmtime_r(&t, &tm);
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%02d:%02d:%02dZ", tm.tm_hour, tm.tm_min, tm.tm_sec);
  return buf;
}

void usage() {
  std::printf(
      "ghostnet-rx: receive-only GhostNet JS8 monitor (RTL-SDR or IQ file)\n\n"
      "  --list                 list RTL-SDR devices\n"
      "  --preset 80m|40m|20m   3.575 / 7.107 / 14.107 MHz (GhostNet V1.5)\n"
      "  --preset test40|test20|test80\n"
      "                         7.078 / 14.078 / 3.578 MHz JS8 calling frequencies:\n"
      "                         busy most hours, for checking reception (not GhostNet)\n"
      "  --freq HZ              dial frequency (USB)\n"
      "  --device N             RTL-SDR index (default 0)\n"
      "  --ppm N                frequency correction\n"
      "  --sample-rate HZ       default 240000 (must be a multiple of 12000)\n"
      "  --offset HZ            dongle tuned this far above the dial (default 25000)\n"
      "  --invert               treat the capture as spectrum-inverted\n"
      "  --lsb                  demodulate LSB instead of USB\n"
      "  --no-rtl-agc           disable the RTL2832U digital AGC\n"
      "  --ds auto|0|1|2        direct sampling (default auto: Q-branch below 24 MHz)\n"
      "  --gain TENTHS_DB       tuner gain (only used above 24 MHz)\n"
      "  --record FILE.cu8      also write raw IQ (+ FILE.cu8.meta)\n"
      "  --iq-file FILE.cu8     replay a recording instead of using hardware\n"
      "  --wav FILE.wav         decode an audio recording (USB audio, any rate/bit depth)\n"
      "                         instead of using hardware. JS8: the file is assumed to start\n"
      "                         on a 15 s slot boundary unless --start-unix-ms is given\n"
      "  --speed X              replay speed (default 8)\n"
      "  --start-unix-ms MS     replay: UTC of the first sample (default from .meta)\n"
      "  --seconds N            stop after N seconds\n"
      "  --mode js8|rtty|lsb|usb|fm|ale\n"
      "                         receive mode (default js8); lsb/usb/fm are live voice\n"
      "  --audio-out FILE|-     voice modes: write 12 kHz s16le mono audio (e.g. pipe to\n"
      "                         aplay -r 12000 -f S16_LE). Never stored by the app itself.\n"
      "  --rtty-center HZ       RTTY audio centre (default: automatic)\n"
      "  --rtty-reverse         RTTY mark/space reversed\n"
      "  --ale-channels LIST    ALE: comma-separated dial Hz to scan, or 'ghostnet' for the\n"
      "                         GhostNet V1.5 10-channel list (default: --freq only)\n"
      "  --ale-dwell MS         ALE scan dwell per channel (default 1000)\n"
      "  --verbose              decoder log\n");
}

// Wraps a source and tees its bytes into a file.
class RecordingSource : public IqSource {
 public:
  RecordingSource(IqSource* inner, std::string path, std::map<std::string, std::string> meta)
      : inner_(inner), path_(std::move(path)), meta_(std::move(meta)) {}
  ~RecordingSource() override { stop(); }

  bool start(DataFn on_data, ErrorFn on_error) override {
    file_ = std::fopen(path_.c_str(), "wb");
    if (!file_) return false;
    const double rate = std::stod(meta_["sample_rate_hz"]);
    return inner_->start(
        [this, rate, on_data = std::move(on_data)](const std::uint8_t* d, std::size_t n) {
          if (!wrote_meta_) {
            // Arrival time of the first chunk, minus its duration, is the
            // closest available estimate of when its first sample was taken.
            meta_["start_unix_ms"] = std::to_string(now_ms() - static_cast<std::int64_t>(n / 2.0 / rate * 1000.0));
            write_capture_meta(path_, meta_);
            wrote_meta_ = true;
          }
          std::fwrite(d, 1, n, file_);
          on_data(d, n);
        },
        std::move(on_error));
  }
  void stop() override {
    inner_->stop();
    if (file_) {
      std::fclose(file_);
      file_ = nullptr;
    }
  }
  std::string description() const override { return inner_->description() + " -> " + path_; }

 private:
  IqSource* inner_;
  std::string path_;
  std::map<std::string, std::string> meta_;
  std::FILE* file_ = nullptr;
  bool wrote_meta_ = false;
};

}  // namespace

int main(int argc, char** argv) {
  ReceiverConfig cfg;
  RtlSdrSettings rtl;
  std::uint32_t device = 0;
  std::string record_path;
  std::string iq_file;
  std::string wav_file;
  double speed = 8.0;
  std::int64_t start_unix_ms = 0;
  double seconds = 0.0;
  bool list = false;
  std::string audio_out;

  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto next = [&]() -> std::string {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "%s needs a value\n", a.c_str());
        std::exit(2);
      }
      return argv[++i];
    };
    if (a == "--list") list = true;
    else if (a == "--preset") {
      const auto p = next();
      cfg.dial_hz = p == "80m" ? 3575000 : p == "40m" ? 7107000 : p == "20m" ? 14107000
                  : p == "test40" ? 7078000 : p == "test20" ? 14078000 : p == "test80" ? 3578000 : 0;
      if (!cfg.dial_hz) { std::fprintf(stderr, "unknown preset %s\n", p.c_str()); return 2; }
    } else if (a == "--freq") cfg.dial_hz = static_cast<std::uint32_t>(std::stoul(next()));
    else if (a == "--device") device = static_cast<std::uint32_t>(std::stoul(next()));
    else if (a == "--ppm") rtl.ppm = std::stoi(next());
    else if (a == "--sample-rate") cfg.sample_rate_hz = static_cast<std::uint32_t>(std::stoul(next()));
    else if (a == "--offset") cfg.capture_offset_hz = std::stoi(next());
    else if (a == "--invert") cfg.invert_spectrum = true;
    else if (a == "--lsb") cfg.sideband = ghostnet::rf::Sideband::kLsb;
    else if (a == "--no-rtl-agc") rtl.rtl_agc = false;
    else if (a == "--ds") { const auto v = next(); rtl.direct_sampling = v == "auto" ? -1 : std::stoi(v); }
    else if (a == "--gain") rtl.tuner_gain_tenths_db = std::stoi(next());
    else if (a == "--record") record_path = next();
    else if (a == "--iq-file") iq_file = next();
    else if (a == "--wav") wav_file = next();
    else if (a == "--speed") speed = std::stod(next());
    else if (a == "--start-unix-ms") start_unix_ms = std::stoll(next());
    else if (a == "--seconds") seconds = std::stod(next());
    else if (a == "--verbose") cfg.verbose_engine_log = true;
    else if (a == "--mode") {
      const auto m = next();
      if (m == "js8") cfg.mode = ReceiveMode::kJs8;
      else if (m == "rtty") cfg.mode = ReceiveMode::kRtty;
      else if (m == "lsb") { cfg.mode = ReceiveMode::kVoiceSsb; cfg.sideband = ghostnet::rf::Sideband::kLsb; }
      else if (m == "usb") { cfg.mode = ReceiveMode::kVoiceSsb; cfg.sideband = ghostnet::rf::Sideband::kUsb; }
      else if (m == "fm") cfg.mode = ReceiveMode::kVoiceFm;
      else if (m == "ale") cfg.mode = ReceiveMode::kAle;
      else { std::fprintf(stderr, "unknown mode %s\n", m.c_str()); return 2; }
    }
    else if (a == "--audio-out") audio_out = next();
    else if (a == "--rtty-center") cfg.rtty.center_hz = std::stod(next());
    else if (a == "--rtty-reverse") cfg.rtty.reverse = true;
    else if (a == "--ale-dwell") cfg.ale.dwell_ms = std::stoi(next());
    else if (a == "--ale-channels") {
      const std::string v = next();
      cfg.ale.channels_hz.clear();
      if (v == "ghostnet") {
        cfg.ale.channels_hz = {3598500, 7104500, 7106500, 7108500, 10128500,
                               14111500, 14113500, 14115500, 18108500, 28108500};
      } else {
        std::size_t pos = 0;
        while (pos < v.size()) {
          const std::size_t comma = v.find(',', pos);
          cfg.ale.channels_hz.push_back(static_cast<std::uint32_t>(std::stoul(v.substr(pos, comma - pos))));
          pos = comma == std::string::npos ? v.size() : comma + 1;
        }
      }
      if (!cfg.ale.channels_hz.empty()) cfg.dial_hz = cfg.ale.channels_hz.front();
    }
    else if (a == "--help" || a == "-h") { usage(); return 0; }
    else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); usage(); return 2; }
  }

  if (list) {
    const auto n = RtlSdrSource::device_count();
    std::printf("%u RTL-SDR device(s)\n", n);
    for (std::uint32_t i = 0; i < n; ++i) std::printf("  #%u %s\n", i, RtlSdrSource::device_name(i).c_str());
    return 0;
  }

  std::unique_ptr<RtlSdrSource> rtlsdr;
  std::unique_ptr<FileIqSource> file;
  std::unique_ptr<RecordingSource> recorder;
  IqSource* source = nullptr;

  std::vector<float> wav_audio;
  if (!wav_file.empty()) {
    WavInfo info;
    std::string error;
    if (!read_wav_mono(wav_file, 12000.0, &wav_audio, &info, &error)) {
      std::fprintf(stderr, "%s\n", error.c_str());
      return 1;
    }
    std::fprintf(stderr, "wav: %s, %d Hz, %d ch, %d bit, %.1f s\n", wav_file.c_str(), info.sample_rate_hz,
                 info.channels, info.bits, info.seconds);
    if (!start_unix_ms) start_unix_ms = (now_ms() / 15000 - 40) * 15000;  // a past slot boundary
    cfg.sample_clock_timestamps = true;
    cfg.replay_start_unix_ms = start_unix_ms;
    cfg.record_path.clear();
  } else if (!iq_file.empty()) {
    auto meta = read_capture_meta(iq_file);
    if (meta.count("dial_hz")) cfg.dial_hz = static_cast<std::uint32_t>(std::stoul(meta["dial_hz"]));
    if (meta.count("sample_rate_hz")) cfg.sample_rate_hz = static_cast<std::uint32_t>(std::stoul(meta["sample_rate_hz"]));
    if (meta.count("capture_offset_hz")) cfg.capture_offset_hz = std::stoi(meta["capture_offset_hz"]);
    if (!start_unix_ms && meta.count("start_unix_ms")) start_unix_ms = std::stoll(meta["start_unix_ms"]);
    if (!start_unix_ms) {
      std::fprintf(stderr,
                   "warning: no start time for %s (no .meta and no --start-unix-ms); JS8 slot alignment will be "
                   "wrong and frames will probably not decode\n",
                   iq_file.c_str());
      start_unix_ms = now_ms();
    }
    cfg.sample_clock_timestamps = true;
    cfg.replay_start_unix_ms = start_unix_ms;
    file = std::make_unique<FileIqSource>(iq_file, cfg.sample_rate_hz, speed);
    source = file.get();
  } else {
    std::string error;
    rtlsdr = RtlSdrSource::open_index(device, &error);
    if (!rtlsdr) {
      std::fprintf(stderr, "%s\n", error.c_str());
      return 1;
    }
    rtl.center_hz = capture_center_hz(cfg);
    rtl.sample_rate_hz = cfg.sample_rate_hz;
    if (!rtlsdr->configure(rtl, &error)) {
      std::fprintf(stderr, "configure failed: %s\n", error.c_str());
      return 1;
    }
    std::fprintf(stderr, "device: %s\n", rtlsdr->description().c_str());
    std::fprintf(stderr, "tuned:  centre %u Hz, %u S/s, direct sampling %d (0 off, 1 I, 2 Q), RTL AGC %s, ppm %d\n",
                rtlsdr->actual_center_hz(), rtlsdr->actual_sample_rate_hz(), rtlsdr->direct_sampling_mode(),
                rtl.rtl_agc ? "on" : "off", rtl.ppm);
    source = rtlsdr.get();
    if (!record_path.empty()) {
      std::map<std::string, std::string> meta{{"dial_hz", std::to_string(cfg.dial_hz)},
                                              {"center_hz", std::to_string(rtl.center_hz)},
                                              {"capture_offset_hz", std::to_string(cfg.capture_offset_hz)},
                                              {"sample_rate_hz", std::to_string(cfg.sample_rate_hz)},
                                              {"ppm", std::to_string(rtl.ppm)},
                                              {"direct_sampling", std::to_string(rtlsdr->direct_sampling_mode())},
                                              {"rtl_agc", rtl.rtl_agc ? "1" : "0"},
                                              {"format", "cu8"}};
      recorder = std::make_unique<RecordingSource>(rtlsdr.get(), record_path, meta);
      source = recorder.get();
    }
  }

  const char* mode_name = cfg.mode == ReceiveMode::kJs8 ? "JS8" : cfg.mode == ReceiveMode::kRtty ? "RTTY 45.45 baud"
                          : cfg.mode == ReceiveMode::kVoiceFm ? "FM voice" : "SSB voice";
  std::fprintf(stderr, "dial:   %u Hz %s, mode %s%s\n\n", cfg.dial_hz,
               cfg.mode == ReceiveMode::kVoiceFm ? "FM" : cfg.sideband == ghostnet::rf::Sideband::kUsb ? "USB" : "LSB",
               mode_name, cfg.invert_spectrum ? ", spectrum inverted" : "");

  ReceiverCallbacks cb;
  std::FILE* audio_file = nullptr;
  if (!audio_out.empty()) audio_file = audio_out == "-" ? stdout : std::fopen(audio_out.c_str(), "wb");
  if (audio_file) {
    cb.on_audio = [audio_file](const std::int16_t* p, std::size_t n) { std::fwrite(p, 2, n, audio_file); };
  }
  cb.on_rtty_char = [](const std::string& t, bool) {
    std::lock_guard<std::mutex> l(g_print);
    std::fputs(t.c_str(), stderr);
  };
  cb.on_rtty_line = [](const RttyLine& line) {
    std::lock_guard<std::mutex> l(g_print);
    std::printf("\n>>> %s RTTY %.0f Hz (%d chars, %d framing errors): %s\n", utc_hms(line.start_ms).c_str(),
                line.center_hz, line.characters, line.framing_errors, line.text.c_str());
    std::fflush(stdout);
  };
  cb.on_ale_word = [](const ale::Word& w) {
    std::fprintf(stdout, "%s ALE %.4f MHz  %-4s %s  (errors %d, votes %d/48)\n", utc_hms(w.time_ms).c_str(),
                 w.channel_hz / 1e6, ale::preamble_name(w.preamble), w.chars, w.errors, 48 - w.bad_votes);
    std::fflush(stdout);
  };
  cb.on_ale_transmission = [](const ale::Transmission& t) {
    std::fprintf(stdout, "%s ALE %.4f MHz  ** %s **\n", utc_hms(t.end_ms).c_str(), t.channel_hz / 1e6,
                 t.summary().c_str());
    std::fflush(stdout);
  };
  cb.on_frame = [](const FrameEvent& f) {
    std::lock_guard<std::mutex> l(g_print);
    std::printf("%s %-6s %+5.0f Hz %+3d dB dt%+.1f  %s%s  %-24s  [%s]\n", utc_hms(f.utc_ms).c_str(),
                submode_name(f.submode), f.offset_hz, f.snr_db, f.dt_s, f.frame.first() ? "F" : "-",
                f.frame.last() ? "L" : "-", f.frame.text.c_str(), f.frame.raw.c_str());
    std::fflush(stdout);
  };
  cb.on_message = [](const AssembledMessage& m) {
    std::lock_guard<std::mutex> l(g_print);
    std::string state = m.complete ? "complete" : "PARTIAL";
    if (m.missing_start) state += ", missing start";
    if (m.has_gap) state += ", missing frame";
    if (m.timed_out) state += ", timed out";
    std::printf(">>> %s %s (%zu frames, %s): %s\n", utc_hms(m.first_utc_ms).c_str(), submode_name(m.submode),
                m.frames.size(), state.c_str(), m.text.c_str());
    std::fflush(stdout);
  };
  cb.on_source_error = [](const std::string& e) {
    std::lock_guard<std::mutex> l(g_print);
    std::fprintf(stderr, "source error: %s\n", e.c_str());
    g_quit = true;
  };
  cb.on_log = [](const std::string& s) {
    std::lock_guard<std::mutex> l(g_print);
    std::fprintf(stderr, "%s\n", s.c_str());
  };

  Receiver rx(cfg, cb);
  std::signal(SIGINT, on_signal);
  std::signal(SIGTERM, on_signal);
  if (!rx.start(source)) {
    std::fprintf(stderr, "receiver failed to start\n");
    return 1;
  }

  const auto t0 = std::chrono::steady_clock::now();
  auto last_status = t0;
  std::thread file_watch;
  std::atomic<bool> file_done{false};
  if (file) file_watch = std::thread([&] { file->wait_finished(); file_done = true; });
  if (!wav_audio.empty()) {
    file_watch = std::thread([&] {
      const auto w0 = std::chrono::steady_clock::now();
      for (std::size_t off = 0; off < wav_audio.size() && !g_quit; off += 1200) {
        rx.push_audio(wav_audio.data() + off, std::min<std::size_t>(1200, wav_audio.size() - off));
        std::this_thread::sleep_until(w0 + std::chrono::duration<double>((off + 1200) / 12000.0 / speed));
      }
      // Trailing silence so the last JS8 window and ALE word complete.
      std::vector<float> tail(12000 * 16, 0.0f);
      rx.push_audio(tail.data(), tail.size());
      file_done = true;
    });
  }

  while (!g_quit) {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    const auto now = std::chrono::steady_clock::now();
    if (seconds > 0 && now - t0 > std::chrono::duration<double>(seconds)) break;
    if (file_done) {
      rx.wait_idle();
      std::this_thread::sleep_for(std::chrono::seconds(3));
      break;
    }
    if (now - last_status > std::chrono::seconds(5)) {
      last_status = now;
      const auto s = rx.stats();
      std::lock_guard<std::mutex> l(g_print);
      std::fprintf(stderr, "[status] IQ %.1f dBFS clip %.2f%% | channel %.1f dBFS AGC %+.0f dB | passes %llu frames %llu "
                  "msgs %llu | dt %+.2f s (%d) | dropped %llu B\n",
                  s.iq_level_dbfs, s.iq_clip_percent, s.audio_level_dbfs, s.agc_gain_db,
                  static_cast<unsigned long long>(s.decode_passes), static_cast<unsigned long long>(s.frames_decoded),
                  static_cast<unsigned long long>(s.messages), s.median_dt_s, s.dt_samples,
                  static_cast<unsigned long long>(s.iq_bytes_dropped));
      std::fflush(stdout);
    }
  }

  rx.stop();
  if (file_watch.joinable()) {
    if (file) file->stop();
    file_watch.join();
  }
  const auto s = rx.stats();
  std::fprintf(stderr, "\nsummary: %llu frames, %llu messages, %llu IQ bytes, %llu dropped\n",
              static_cast<unsigned long long>(s.frames_decoded), static_cast<unsigned long long>(s.messages),
              static_cast<unsigned long long>(s.iq_bytes), static_cast<unsigned long long>(s.iq_bytes_dropped));
  return 0;
}
