#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ghostnet/rf/ssb_demodulator.hpp"
#include "ghostnet/rx/ale.hpp"
#include "ghostnet/rx/iq_source.hpp"
#include "ghostnet/rx/message_assembler.hpp"
#include "ghostnet/rx/rtty.hpp"

namespace ghostnet::rx {

enum class ReceiveMode {
  kJs8,       // digital: JS8 decoder, frames + messages
  kRtty,      // digital: 45.45 baud RTTY decoder, text lines
  kVoiceSsb,  // analog: live SSB audio (LSB for the 7.190 MHz fallback)
  kVoiceFm,   // analog: live narrowband FM audio (VHF/UHF)
  kAle,       // digital: 2G ALE (MIL-STD-188-141) words, calls and soundings; optional scanning
};

inline bool is_voice(ReceiveMode m) { return m == ReceiveMode::kVoiceSsb || m == ReceiveMode::kVoiceFm; }

struct ReceiverConfig {
  ReceiveMode mode = ReceiveMode::kJs8;

  // Published dial frequency (USB suppressed carrier), e.g. 7107000.
  std::uint32_t dial_hz = 7107000;

  // RTL2832U sample rate. 240 kS/s is an exact 20x multiple of the 12 kHz
  // decoder rate and is inside the chip's 225-300 kS/s band.
  std::uint32_t sample_rate_hz = 240000;

  // The dongle is tuned to dial + capture_offset_hz so its DC/LO spike sits
  // well away from the 0-2.8 kHz audio channel.
  std::int32_t capture_offset_hz = 25000;

  rf::Sideband sideband = rf::Sideband::kUsb;
  bool invert_spectrum = false;

  // js8core SubmodeId bitmask: bit0 Normal(A), bit1 Fast(B), bit2 Turbo(C),
  // bit3 Slow(E), bit4 Ultra(I). GhostNet nets use Normal.
  int submode_mask = 0b01111;

  // Bounded queue between the USB thread and the DSP thread. When full,
  // incoming data is dropped and counted rather than queued without limit.
  std::size_t queue_limit_bytes = 4u << 20;  // ~8.7 s at 240 kS/s

  // Replay: derive timestamps from the sample count starting at
  // replay_start_unix_ms instead of from arrival time.
  bool sample_clock_timestamps = false;
  std::int64_t replay_start_unix_ms = 0;

  bool verbose_engine_log = false;

  // RTTY options (mode kRtty).
  RttyConfig rtty;

  // 2G ALE (mode kAle). With more than one channel the receiver scans:
  // it dwells on each channel (USB dial frequencies), stops on any decoded
  // word, and resumes hold_ms after the last word heard. dial_hz is ignored
  // when channels is non-empty. Raw IQ recording is off while scanning (a
  // recording has one frequency).
  struct AleConfig {
    std::vector<std::uint32_t> channels_hz;
    int dwell_ms = 1000;
    int hold_ms = 4000;
  } ale;

  // Voice (modes kVoiceSsb / kVoiceFm). Squelch applies to FM only: audio is
  // muted while the noise metric is above the threshold.
  bool fm_squelch = true;
  float fm_squelch_threshold = 0.5f;

  // Raw IQ recording (digital modes only; analog audio is never recorded,
  // per the spec, so this is ignored in voice modes): when non-empty, every IQ byte handed to the
  // demodulator is also written here (dropped spans as neutral samples, so
  // replay timing stays exact), with "<path>.meta" alongside. Recording
  // stops on its own after record_max_bytes (0 = no limit).
  std::string record_path;
  std::uint64_t record_max_bytes = 0;
};

std::uint32_t capture_center_hz(const ReceiverConfig& config);

struct ReceiverStats {
  bool running = false;
  std::uint64_t iq_bytes = 0;
  std::uint64_t iq_bytes_dropped = 0;
  std::uint64_t audio_samples = 0;
  double iq_level_dbfs = -200.0;  // RMS of raw IQ vs full scale
  double iq_clip_percent = 0.0;   // share of IQ bytes pinned at 0 or 255
  double audio_level_dbfs = -200.0;  // demodulated channel before AGC
  double agc_gain_db = 0.0;
  double spectrum_power_db = 0.0;
  double spectrum_peak_db = 0.0;
  std::uint64_t decode_passes = 0;
  std::uint64_t frames_decoded = 0;
  std::uint64_t messages = 0;
  double median_dt_s = 0.0;  // clock health: JS8 timing offset of recent decodes
  int dt_samples = 0;
  std::int64_t last_iq_unix_ms = 0;
  std::int64_t last_frame_unix_ms = 0;
  bool recording = false;
  std::uint64_t recorded_bytes = 0;
  // Voice
  bool squelch_open = false;
  float fm_noise = 1.0f;
  // RTTY
  bool rtty_locked = false;
  double rtty_center_hz = 0.0;
  float rtty_quality = 0.0f;
  std::uint64_t rtty_characters = 0;
  std::uint64_t rtty_framing_errors = 0;
  // ALE
  std::uint32_t ale_channel_hz = 0;  // dial frequency being listened to
  int ale_channel_index = 0;
  bool ale_scanning = false;         // more than one channel configured
  bool ale_holding = false;          // stopped on activity
  bool ale_sync = false;
  float ale_quality = 0.0f;
  std::uint64_t ale_words = 0;
  std::uint64_t ale_transmissions = 0;
  std::uint64_t retunes = 0;
  std::uint64_t retune_failures = 0;
};

struct ReceiverCallbacks {
  std::function<void(const FrameEvent&)> on_frame;            // every unique decoded frame
  std::function<void(const AssembledMessage&)> on_message;    // assembled / closed messages
  std::function<void(const std::string&)> on_source_error;    // device lost, read failure
  std::function<void(const std::string&)> on_log;
  // Voice modes: demodulated audio, int16 mono at 12 kHz, for live playback
  // only. Never stored.
  std::function<void(const std::int16_t* pcm, std::size_t count)> on_audio;
  // RTTY: each decoded character as it arrives, and each completed line.
  std::function<void(const std::string& text, bool framing_error)> on_rtty_char;
  std::function<void(const RttyLine& line)> on_rtty_line;
  // ALE: every decoded word (with its channel), and each assembled
  // transmission (sounding, call) when it ends.
  std::function<void(const ale::Word&)> on_ale_word;
  std::function<void(const ale::Transmission&)> on_ale_transmission;
  // Waterfall rows (dB, 0-3 kHz above the dial, 10 Hz bins), ~12 per second.
  std::function<void(const std::vector<float>& bins, float bin_hz)> on_spectrum;
};

// RTL-SDR IQ -> demodulator (SSB or FM) -> one of: js8core decoder (frames,
// messages), RTTY decoder (text lines), or live audio (voice).
//
// Receive-only: the js8core engine is created with no audio output, no rig
// control and no network channel, and this class never calls any of the
// engine's transmit methods. The engine's transmit path additionally needs
// set_tx_ready(true), which nothing in this codebase calls.
class Receiver {
 public:
  Receiver(ReceiverConfig config, ReceiverCallbacks callbacks);
  ~Receiver();

  Receiver(const Receiver&) = delete;
  Receiver& operator=(const Receiver&) = delete;

  // Starts the DSP/decoder and then the source. `source` must outlive the
  // Receiver or the next stop(). Pass nullptr to drive it with push_iq().
  bool start(IqSource* source);

  // Stops the source, the DSP thread and the decoder, then closes any
  // half-received messages (delivered through on_message as incomplete).
  // No callbacks are invoked after this returns.
  void stop();

  // Feed IQ directly (tests, replay without an IqSource).
  void push_iq(const std::uint8_t* iq, std::size_t bytes);

  // Blocks until everything queued so far has been demodulated and handed
  // to the decoder. Test/replay helper.
  void wait_idle();

  ReceiverStats stats() const;
  const ReceiverConfig& config() const { return config_; }

 private:
  struct Impl;
  ReceiverConfig config_;
  ReceiverCallbacks callbacks_;
  std::unique_ptr<Impl> impl_;  // recreated on each start(): every session starts clean
};

}  // namespace ghostnet::rx
