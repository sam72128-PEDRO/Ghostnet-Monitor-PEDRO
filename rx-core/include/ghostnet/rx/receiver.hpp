#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ghostnet/rf/ssb_demodulator.hpp"
#include "ghostnet/rx/iq_source.hpp"
#include "ghostnet/rx/message_assembler.hpp"

namespace ghostnet::rx {

struct ReceiverConfig {
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
};

struct ReceiverCallbacks {
  std::function<void(const FrameEvent&)> on_frame;            // every unique decoded frame
  std::function<void(const AssembledMessage&)> on_message;    // assembled / closed messages
  std::function<void(const std::string&)> on_source_error;    // device lost, read failure
  std::function<void(const std::string&)> on_log;
  std::function<void(const std::vector<float>& bins, float bin_hz)> on_spectrum;  // ~2 Hz
};

// RTL-SDR IQ -> SSB demod -> AGC -> js8core decoder -> frames -> messages.
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
