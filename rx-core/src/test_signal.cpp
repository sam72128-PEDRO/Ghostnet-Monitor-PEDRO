#include "ghostnet/rx/test_signal.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <mutex>
#include <random>
#include <thread>

#include "self_test_tones.inc"

namespace ghostnet::rx {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr int kPeriodMs = 15000;
constexpr double kSymbolSeconds = 1920.0 / 12000.0;  // JS8 Normal
constexpr double kBaud = 1.0 / kSymbolSeconds;

std::int64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
      .count();
}
}  // namespace

TestSignal synthesize_test_signal(const std::vector<Js8Tones>& frames, const TestSignalParams& p) {
  TestSignal sig;
  sig.frames = frames.size();
  const double fs = p.sample_rate_hz;
  const std::int64_t slots = static_cast<std::int64_t>(frames.size()) + 2;
  sig.first_slot_unix_ms = (now_ms() / kPeriodMs - slots - 2) * kPeriodMs;
  sig.start_unix_ms = sig.first_slot_unix_ms - 2000;
  const double duration_s = 2.0 + static_cast<double>(slots) * kPeriodMs / 1000.0;
  const auto pairs = static_cast<std::size_t>(duration_s * fs);

  std::vector<float> freq(pairs, 0.0f);
  for (std::size_t f = 0; f < frames.size(); ++f) {
    const double start_s = 2.0 + static_cast<double>(f) * kPeriodMs / 1000.0 + 0.5;
    for (int k = 0; k < kJs8Symbols; ++k) {
      const auto a = static_cast<std::size_t>((start_s + k * kSymbolSeconds) * fs);
      const auto b = static_cast<std::size_t>((start_s + (k + 1) * kSymbolSeconds) * fs);
      const float hz = static_cast<float>(p.audio_hz + frames[f][static_cast<std::size_t>(k)] * kBaud);
      for (std::size_t n = a; n < b && n < pairs; ++n) freq[n] = hz;
    }
  }

  std::mt19937 rng(p.seed);
  std::normal_distribution<double> noise(0.0, p.noise_counts);
  const double interferer_hz = 9000.0 - p.capture_offset_hz;
  double phase = 0.0;
  double iphase = 0.0;
  sig.iq.resize(2 * pairs);
  for (std::size_t n = 0; n < pairs; ++n) {
    std::complex<double> v(0.0, 0.0);
    if (freq[n] > 0.0f) {
      phase = std::remainder(phase + 2.0 * kPi * (freq[n] - p.capture_offset_hz) / fs, 2.0 * kPi);
      v += std::polar(p.signal_counts, phase);
    }
    iphase = std::remainder(iphase + 2.0 * kPi * interferer_hz / fs, 2.0 * kPi);
    v += std::polar(p.interferer_counts, iphase);
    if (p.mirrored) v = std::conj(v);
    const double i = 127.5 + p.dc_i_counts + v.real() + noise(rng);
    const double q = 127.5 + p.dc_q_counts + v.imag() + noise(rng);
    sig.iq[2 * n] = static_cast<std::uint8_t>(std::clamp(std::lround(i), 0L, 255L));
    sig.iq[2 * n + 1] = static_cast<std::uint8_t>(std::clamp(std::lround(q), 0L, 255L));
  }
  return sig;
}

SelfTestResult run_self_test(double speed) {
  SelfTestResult result;
  result.expected_text = kSelfTestText;
  std::vector<Js8Tones> frames(std::begin(kSelfTestTones), std::end(kSelfTestTones));
  result.frames_expected = frames.size();

  TestSignalParams params;
  params.noise_counts = 6.0;  // a little noisier than "clean"; still a comfortable decode
  const TestSignal sig = synthesize_test_signal(frames, params);
  result.capture_seconds = static_cast<double>(sig.iq.size()) / 2.0 / params.sample_rate_hz;

  std::mutex m;
  std::vector<AssembledMessage> messages;
  std::size_t frame_count = 0;
  ReceiverCallbacks cb;
  cb.on_frame = [&](const FrameEvent&) {
    std::lock_guard<std::mutex> l(m);
    ++frame_count;
  };
  cb.on_message = [&](const AssembledMessage& msg) {
    std::lock_guard<std::mutex> l(m);
    messages.push_back(msg);
  };

  ReceiverConfig cfg;
  cfg.sample_rate_hz = params.sample_rate_hz;
  cfg.capture_offset_hz = params.capture_offset_hz;
  cfg.sample_clock_timestamps = true;
  cfg.replay_start_unix_ms = sig.start_unix_ms;
  cfg.submode_mask = 0b01111;

  const auto t0 = std::chrono::steady_clock::now();
  Receiver rx(cfg, cb);
  if (!rx.start(nullptr)) {
    result.detail = "receiver failed to start";
    return result;
  }
  const std::size_t chunk = 16 * 1024;
  for (std::size_t off = 0; off < sig.iq.size(); off += chunk) {
    const std::size_t n = std::min(chunk, sig.iq.size() - off);
    rx.push_iq(sig.iq.data() + off, n);
    const double t = static_cast<double>(off + n) / 2.0 / params.sample_rate_hz / speed;
    std::this_thread::sleep_until(t0 + std::chrono::duration<double>(t));
  }
  rx.wait_idle();
  std::this_thread::sleep_for(std::chrono::milliseconds(1500));
  const auto stats = rx.stats();
  rx.stop();
  result.wall_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

  std::lock_guard<std::mutex> l(m);
  result.frames_decoded = frame_count;
  for (const auto& msg : messages) {
    if (msg.complete) result.decoded_text = msg.text;
  }
  result.passed = result.decoded_text == result.expected_text && frame_count == frames.size();
  result.detail = "decode passes " + std::to_string(stats.decode_passes) + ", frames " + std::to_string(frame_count) +
                  "/" + std::to_string(frames.size()) + ", dropped IQ bytes " + std::to_string(stats.iq_bytes_dropped);
  return result;
}

}  // namespace ghostnet::rx
