#include "ghostnet/rx/receiver.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <span>
#include <thread>

#include "ghostnet/rx/file_iq_source.hpp"
#include "ghostnet/rx/spectrum.hpp"
#include "js8core/engine.hpp"

namespace ghostnet::rx {

namespace {

using Steady = std::chrono::steady_clock;
using System = std::chrono::system_clock;

constexpr double kAudioRate = 12000.0;
constexpr auto kReanchorLate = std::chrono::milliseconds(250);

std::int64_t unix_ms(System::time_point t) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(t.time_since_epoch()).count();
}

// Maps a steady-clock instant to wall-clock milliseconds, the same way the
// js8core engine does when it aligns its ring to the UTC minute.
std::int64_t wall_ms(Steady::time_point t) {
  const auto offset = std::chrono::duration_cast<System::duration>(t - Steady::now());
  return unix_ms(System::now() + offset);
}

Steady::duration seconds(double s) {
  return std::chrono::duration_cast<Steady::duration>(std::chrono::duration<double>(s));
}

double to_db(double power) { return 10.0 * std::log10(std::max(power, 1e-20)); }

std::int64_t floor_div(std::int64_t a, std::int64_t b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

// The engine's ring is indexed by position within the UTC minute (12 samples
// per ms) and decode windows start on slot boundaries, so a window position
// gives its start's offset into the minute exactly. Returns the most recent
// such instant whose window (of `window_ms`) could already be complete at
// audio time `audio_now_ms`. Unambiguous for decoder lag under ~55 s.
std::int64_t window_start_ms(int window_pos, std::int64_t window_ms, std::int64_t audio_now_ms) {
  const std::int64_t minute_phase_ms = window_pos / 12;
  const std::int64_t latest_start = audio_now_ms - window_ms + 2000;
  return floor_div(latest_start - minute_phase_ms, 60000) * 60000 + minute_phase_ms;
}

}  // namespace

std::uint32_t capture_center_hz(const ReceiverConfig& config) {
  return static_cast<std::uint32_t>(static_cast<std::int64_t>(config.dial_hz) + config.capture_offset_hz);
}

struct Receiver::Impl {
  struct Chunk {
    std::vector<std::uint8_t> data;
    Steady::time_point arrival;
    std::uint64_t gap_pairs = 0;  // pairs dropped immediately before this chunk
  };

  Impl(const ReceiverConfig& cfg, ReceiverCallbacks cbs)
      : config(cfg),
        callbacks(std::move(cbs)),
        demod(make_demod_config(cfg)),
        agc(kAudioRate),
        spectrum(kAudioRate),
        assembler([this](const AssembledMessage& m) { pending_messages.push_back(m); }) {}

  static rf::SsbDemodulatorConfig make_demod_config(const ReceiverConfig& cfg) {
    rf::SsbDemodulatorConfig d;
    d.input_sample_rate_hz = cfg.sample_rate_hz;
    d.audio_sample_rate_hz = kAudioRate;
    d.tune_offset_hz = -static_cast<double>(cfg.capture_offset_hz);
    d.sideband = cfg.sideband;
    d.invert_spectrum = cfg.invert_spectrum;
    return d;
  }

  // ---- configuration / collaborators ----
  ReceiverConfig config;
  ReceiverCallbacks callbacks;
  rf::SsbDemodulator demod;
  rf::SlowAgc agc;
  SpectrumAnalyzer spectrum;
  std::unique_ptr<js8core::Js8Engine> engine;
  IqSource* source = nullptr;

  // ---- USB thread -> DSP thread queue ----
  std::mutex queue_mutex;
  std::condition_variable queue_cv;
  std::condition_variable idle_cv;
  std::deque<Chunk> queue;
  std::size_t queue_bytes = 0;
  std::uint64_t pending_gap_pairs = 0;
  bool stopping = false;
  bool busy = false;
  std::thread dsp_thread;

  // ---- DSP thread state (only touched on the DSP thread) ----
  bool anchored = false;
  std::uint64_t anchor_pairs = 0;
  Steady::time_point anchor_time;
  std::uint64_t pairs_total = 0;
  std::uint64_t gap_carry_pairs = 0;
  std::vector<float> audio;
  std::vector<std::int16_t> pcm;
  Steady::time_point last_tick;
  std::FILE* record_file = nullptr;
  bool record_meta_written = false;
  std::uint64_t recorded_bytes = 0;

  // ---- shared ----
  std::atomic<std::int64_t> latest_audio_wall_ms{0};
  // Start (ring index) of the decode window the following Decoded events
  // came from; only touched on the engine's serial event-dispatch thread.
  int window_pos = -1;
  std::int64_t pass_decoded_through_ms = 0;
  // End of the newest window the decoder has finished. Message timeouts run
  // on this clock, not the audio clock, so a lagging decoder cannot make
  // the assembler give up on a message whose next frame isn't decoded yet.
  std::atomic<std::int64_t> decoded_through_ms{0};
  std::atomic<bool> callbacks_active{false};
  std::mutex assembler_mutex;
  MessageAssembler assembler;
  std::vector<AssembledMessage> pending_messages;  // guarded by assembler_mutex

  mutable std::mutex stats_mutex;
  ReceiverStats stats;
  std::deque<float> recent_dt;

  // ------------------------------------------------------------------------

  void log(const std::string& message) {
    if (callbacks_active && callbacks.on_log) callbacks.on_log(message);
  }

  bool start(IqSource* src) {
    js8core::EngineConfig ec;
    ec.sample_rate_hz = static_cast<int>(kAudioRate);
    ec.submodes = config.submode_mask;

    js8core::EngineCallbacks ecb;
    ecb.on_event = [this](const js8core::events::Variant& ev) { on_engine_event(ev); };
    ecb.on_error = [this](std::string_view m) { log("js8 engine error: " + std::string(m)); };
    if (config.verbose_engine_log) {
      ecb.on_log = [this](js8core::LogLevel, std::string_view m) { log("js8: " + std::string(m)); };
    }

    // Receive-only: no audio output, no rig, no UDP, no scheduler. The
    // engine's transmit methods are never called from this codebase.
    js8core::EngineDependencies deps;
    engine = js8core::make_engine(ec, std::move(ecb), deps);
    if (!engine) return false;

    // Sync stats make the decoder announce each window it scans (SyncStart),
    // which is how decodes are attributed to their exact UTC slot. The
    // engine may drop the high-volume SyncState progress events under load,
    // but never SyncStart or Decoded, and it delivers events in order.
    engine->set_sync_stats(true);

    callbacks_active = true;
    if (!engine->start()) {
      callbacks_active = false;
      engine.reset();
      return false;
    }

    if (config.sample_clock_timestamps) {
      anchored = true;
      anchor_pairs = 0;
      const auto since = std::chrono::milliseconds(unix_ms(System::now()) - config.replay_start_unix_ms);
      anchor_time = Steady::now() - std::chrono::duration_cast<Steady::duration>(since);
    }

    {
      std::lock_guard<std::mutex> lock(stats_mutex);
      stats = ReceiverStats{};
      stats.running = true;
    }
    if (!config.record_path.empty()) {
      record_file = std::fopen(config.record_path.c_str(), "wb");
      if (!record_file) log("could not open IQ recording file " + config.record_path);
      std::lock_guard<std::mutex> lock(stats_mutex);
      stats.recording = record_file != nullptr;
    }

    stopping = false;
    last_tick = Steady::now();
    dsp_thread = std::thread([this] { dsp_loop(); });

    source = src;
    if (source) {
      const bool ok = source->start([this](const std::uint8_t* d, std::size_t n) { push(d, n); },
                                    [this](const std::string& e) {
                                      if (callbacks_active && callbacks.on_source_error) callbacks.on_source_error(e);
                                    });
      if (!ok) {
        source = nullptr;
        stop();
        return false;
      }
    }
    return true;
  }

  void stop() {
    if (source) {
      source->stop();
      source = nullptr;
    }
    {
      std::lock_guard<std::mutex> lock(queue_mutex);
      stopping = true;
    }
    queue_cv.notify_all();
    if (dsp_thread.joinable()) dsp_thread.join();
    close_recording("session stopped");
    if (engine) {
      engine->stop();
      engine.reset();
    }
    {
      std::lock_guard<std::mutex> lock(assembler_mutex);
      assembler.flush();
    }
    deliver_pending();
    callbacks_active = false;
    std::lock_guard<std::mutex> lock(stats_mutex);
    stats.running = false;
  }

  void push(const std::uint8_t* data, std::size_t bytes) {
    const auto arrival = Steady::now();
    bytes &= ~static_cast<std::size_t>(1);
    if (bytes == 0) return;
    {
      std::lock_guard<std::mutex> lock(queue_mutex);
      if (stopping) return;
      if (queue_bytes + bytes > config.queue_limit_bytes) {
        pending_gap_pairs += bytes / 2;
        std::lock_guard<std::mutex> s(stats_mutex);
        stats.iq_bytes_dropped += bytes;
        return;
      }
      Chunk c;
      c.data.assign(data, data + bytes);
      c.arrival = arrival;
      c.gap_pairs = pending_gap_pairs;
      pending_gap_pairs = 0;
      queue_bytes += bytes;
      queue.push_back(std::move(c));
    }
    queue_cv.notify_one();
  }

  void wait_idle() {
    std::unique_lock<std::mutex> lock(queue_mutex);
    idle_cv.wait(lock, [this] { return (queue.empty() && !busy) || stopping; });
  }

  void dsp_loop() {
    for (;;) {
      Chunk chunk;
      {
        std::unique_lock<std::mutex> lock(queue_mutex);
        queue_cv.wait(lock, [this] { return stopping || !queue.empty(); });
        if (stopping) break;
        chunk = std::move(queue.front());
        queue.pop_front();
        queue_bytes -= chunk.data.size();
        busy = true;
      }
      process(chunk);
      {
        std::lock_guard<std::mutex> lock(queue_mutex);
        busy = false;
      }
      idle_cv.notify_all();
    }
    idle_cv.notify_all();
  }

  void process(const Chunk& chunk) {
    const double fs = config.sample_rate_hz;
    const std::uint64_t pairs = chunk.data.size() / 2;
    const std::uint64_t start_pair = pairs_total + chunk.gap_pairs;
    const std::uint64_t end_pair = start_pair + pairs;
    pairs_total = end_pair;

    // Live timing: USB delivery only ever adds delay, so the earliest
    // arrival relative to the sample count is the best estimate of when
    // samples were actually taken. Re-anchor on any earlier arrival, and on
    // a large late jump (a stall or dropped data).
    if (!config.sample_clock_timestamps) {
      const auto predicted = anchor_time + seconds(static_cast<double>(end_pair - anchor_pairs) / fs);
      if (!anchored || chunk.arrival < predicted || chunk.arrival - predicted > kReanchorLate) {
        anchored = true;
        anchor_pairs = end_pair;
        anchor_time = chunk.arrival;
      }
    }
    const auto chunk_start_time =
        anchor_time + seconds((static_cast<double>(start_pair) - static_cast<double>(anchor_pairs)) / fs);

    if (record_file) record_chunk(chunk, chunk_start_time);

    // IQ level / clipping, for the Signal indicator and gain advice.
    double power = 0.0;
    std::uint64_t clipped = 0;
    for (std::uint8_t b : chunk.data) {
      const double v = (static_cast<double>(b) - 127.5) / 127.5;
      power += v * v;
      clipped += (b == 0 || b == 255);
    }
    power /= static_cast<double>(chunk.data.size());

    audio.clear();
    demod.process_iq_u8(chunk.data.data(), pairs, &audio);
    // Samples dropped before this chunk are replaced by the equivalent
    // stretch of silence, so the decoder's ring stays aligned to UTC. The
    // engine only realigns on jumps over 250 ms, so without this, repeated
    // small drops would each shift slot timing and could add up to seconds.
    std::size_t pad = 0;
    if (chunk.gap_pairs > 0) {
      const std::uint64_t decimation = config.sample_rate_hz / static_cast<std::uint32_t>(kAudioRate);
      gap_carry_pairs += chunk.gap_pairs;
      pad = static_cast<std::size_t>(gap_carry_pairs / decimation);
      gap_carry_pairs %= decimation;
    }
    pcm.assign(pad, 0);
    agc.process(audio.data(), audio.size(), &pcm);
    if (callbacks.on_spectrum) {
      spectrum.process(audio.data(), audio.size(), [this](const std::vector<float>& row, float bin_hz) {
        if (callbacks_active) callbacks.on_spectrum(row, bin_hz);
      });
    }

    if (!pcm.empty() && engine) {
      const auto captured_at =
          chunk_start_time - seconds(demod.latency_seconds() + static_cast<double>(pad) / kAudioRate);
      js8core::AudioInputBuffer buffer{std::as_bytes(std::span<const std::int16_t>(pcm.data(), pcm.size())),
                                       js8core::AudioFormat{static_cast<int>(kAudioRate), 1,
                                                            js8core::SampleType::Int16},
                                       captured_at};
      engine->submit_capture(buffer);
      latest_audio_wall_ms = wall_ms(captured_at + seconds(static_cast<double>(pcm.size()) / kAudioRate));
    }

    {
      std::lock_guard<std::mutex> lock(stats_mutex);
      const double a = 0.2;
      const double level = to_db(power * 2.0);  // full-scale complex tone has power 1, sine 0.5 per axis
      stats.iq_level_dbfs = stats.iq_bytes == 0 ? level : (1 - a) * stats.iq_level_dbfs + a * level;
      const double clip = 100.0 * static_cast<double>(clipped) / static_cast<double>(chunk.data.size());
      stats.iq_clip_percent = stats.iq_bytes == 0 ? clip : (1 - a) * stats.iq_clip_percent + a * clip;
      stats.iq_bytes += chunk.data.size();
      stats.audio_samples += pcm.size();
      stats.audio_level_dbfs = agc.input_level_dbfs();
      stats.agc_gain_db = 20.0 * std::log10(std::max(agc.gain(), 1e-12));
      stats.last_iq_unix_ms = wall_ms(chunk.arrival);
    }

    const auto now = Steady::now();
    if (now - last_tick > std::chrono::seconds(1) || config.sample_clock_timestamps) {
      last_tick = now;
      {
        std::lock_guard<std::mutex> lock(assembler_mutex);
        const std::int64_t through = decoded_through_ms.load();
        if (through > 0) assembler.tick(through);
      }
      deliver_pending();
    }
  }

  void record_chunk(const Chunk& chunk, Steady::time_point chunk_start_time) {
    if (!record_meta_written) {
      record_meta_written = true;
      write_capture_meta(config.record_path,
                         {{"dial_hz", std::to_string(config.dial_hz)},
                          {"center_hz", std::to_string(capture_center_hz(config))},
                          {"capture_offset_hz", std::to_string(config.capture_offset_hz)},
                          {"sample_rate_hz", std::to_string(config.sample_rate_hz)},
                          {"invert_spectrum", config.invert_spectrum ? "1" : "0"},
                          {"start_unix_ms", std::to_string(wall_ms(chunk_start_time))},
                          {"format", "cu8"},
                          {"recorder", "GhostNet Monitor"}});
    }
    // Dropped spans are written as neutral samples so a replay keeps the
    // same timeline the live decoder saw.
    if (chunk.gap_pairs > 0) {
      static const std::uint8_t neutral[2] = {127, 128};
      for (std::uint64_t i = 0; i < chunk.gap_pairs && record_file; ++i) std::fwrite(neutral, 1, 2, record_file);
      recorded_bytes += chunk.gap_pairs * 2;
    }
    if (record_file) {
      std::fwrite(chunk.data.data(), 1, chunk.data.size(), record_file);
      recorded_bytes += chunk.data.size();
    }
    {
      std::lock_guard<std::mutex> lock(stats_mutex);
      stats.recorded_bytes = recorded_bytes;
    }
    if (config.record_max_bytes > 0 && recorded_bytes >= config.record_max_bytes) {
      close_recording("size limit reached");
    }
  }

  void close_recording(const std::string& why) {
    if (!record_file) return;
    std::fclose(record_file);
    record_file = nullptr;
    log("IQ recording closed (" + why + "): " + std::to_string(recorded_bytes) + " bytes in " + config.record_path);
    std::lock_guard<std::mutex> lock(stats_mutex);
    stats.recording = false;
  }

  void on_engine_event(const js8core::events::Variant& ev) {
    if (!callbacks_active) return;
    if (const auto* w = std::get_if<js8core::events::SyncStart>(&ev)) {
      window_pos = w->position;
      const std::int64_t window_ms = w->size / 12;
      pass_decoded_through_ms = std::max(
          pass_decoded_through_ms, window_start_ms(w->position, window_ms, latest_audio_wall_ms.load()) + window_ms);
    } else if (std::holds_alternative<js8core::events::DecodeStarted>(ev)) {
      window_pos = -1;
      pass_decoded_through_ms = 0;
    } else if (const auto* d = std::get_if<js8core::events::Decoded>(&ev)) {
      on_decoded(*d);
    } else if (std::holds_alternative<js8core::events::DecodeFinished>(ev)) {
      if (pass_decoded_through_ms > decoded_through_ms.load()) decoded_through_ms = pass_decoded_through_ms;
      std::lock_guard<std::mutex> lock(stats_mutex);
      ++stats.decode_passes;
    } else if (const auto* s = std::get_if<js8core::events::Spectrum>(&ev)) {
      {
        std::lock_guard<std::mutex> lock(stats_mutex);
        stats.spectrum_power_db = s->power_db;
        stats.spectrum_peak_db = s->peak_db;
      }
    }
  }

  void on_decoded(const js8core::events::Decoded& d) {
    const int submode = submode_from_event_mode(d.mode);
    if (submode < 0) return;

    const std::int64_t audio_now = latest_audio_wall_ms.load();
    const std::int64_t latest_start = audio_now - submode_frame_end_ms(submode);
    std::int64_t slot = 0;
    if (window_pos >= 0) {
      slot = window_start_ms(window_pos, submode_frame_end_ms(submode), audio_now);
    } else {
      // Fallback: decodes arrive after their frame ends and before the next
      // slot's frame could have, as long as the decoder isn't lagging.
      const std::int64_t period = submode_period_ms(submode);
      slot = floor_div(latest_start, period) * period;
    }

    FrameEvent fe;
    fe.utc_ms = slot;
    fe.offset_hz = d.frequency;
    fe.snr_db = d.snr;
    fe.submode = submode;
    fe.dt_s = d.xdt;
    fe.quality = d.quality;
    fe.frame = unpack_frame(d.data, d.type);

    bool unique = false;
    {
      std::lock_guard<std::mutex> lock(assembler_mutex);
      unique = assembler.on_frame(fe);
    }
    if (!unique) {
      deliver_pending();
      return;
    }
    {
      std::lock_guard<std::mutex> lock(stats_mutex);
      ++stats.frames_decoded;
      stats.last_frame_unix_ms = unix_ms(System::now());
      recent_dt.push_back(d.xdt);
      if (recent_dt.size() > 25) recent_dt.pop_front();
      std::vector<float> sorted(recent_dt.begin(), recent_dt.end());
      std::nth_element(sorted.begin(), sorted.begin() + static_cast<long>(sorted.size() / 2), sorted.end());
      stats.median_dt_s = sorted[sorted.size() / 2];
      stats.dt_samples = static_cast<int>(sorted.size());
    }
    if (callbacks.on_frame) callbacks.on_frame(fe);
    deliver_pending();
  }

  // Messages are collected under assembler_mutex and handed to the owner
  // here, outside any lock, after the frame that completed them.
  void deliver_pending() {
    std::vector<AssembledMessage> ready;
    {
      std::lock_guard<std::mutex> lock(assembler_mutex);
      ready.swap(pending_messages);
    }
    for (const auto& m : ready) {
      {
        std::lock_guard<std::mutex> lock(stats_mutex);
        ++stats.messages;
      }
      if (callbacks_active && callbacks.on_message) callbacks.on_message(m);
    }
  }
};

Receiver::Receiver(ReceiverConfig config, ReceiverCallbacks callbacks)
    : config_(config), callbacks_(std::move(callbacks)), impl_(std::make_unique<Impl>(config_, callbacks_)) {}

Receiver::~Receiver() { stop(); }

bool Receiver::start(IqSource* source) {
  {
    std::lock_guard<std::mutex> lock(impl_->stats_mutex);
    if (impl_->stats.running) return false;
  }
  impl_ = std::make_unique<Impl>(config_, callbacks_);
  return impl_->start(source);
}

void Receiver::stop() {
  bool running = false;
  {
    std::lock_guard<std::mutex> lock(impl_->stats_mutex);
    running = impl_->stats.running;
  }
  if (running) impl_->stop();
}

void Receiver::push_iq(const std::uint8_t* iq, std::size_t bytes) { impl_->push(iq, bytes); }

void Receiver::wait_idle() { impl_->wait_idle(); }

ReceiverStats Receiver::stats() const {
  std::lock_guard<std::mutex> lock(impl_->stats_mutex);
  return impl_->stats;
}

}  // namespace ghostnet::rx
