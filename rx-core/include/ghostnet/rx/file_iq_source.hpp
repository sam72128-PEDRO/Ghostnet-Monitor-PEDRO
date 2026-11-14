#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <thread>

#include "ghostnet/rx/iq_source.hpp"

namespace ghostnet::rx {

// Replays a raw RTL-SDR capture (.cu8: interleaved u8 I/Q, the same format
// `rtl_sdr` and `ghostnet-rx --record` write). Pair with
// ReceiverConfig::sample_clock_timestamps so decodes line up with the UTC
// slots the capture was recorded in.
class FileIqSource : public IqSource {
 public:
  // speed: 1.0 = real time, 10.0 = ten times faster. Keep it at or below
  // ~10: faster than that and the decoder can fall behind the capture.
  FileIqSource(std::string path, std::uint32_t sample_rate_hz, double speed);
  ~FileIqSource() override;

  bool start(DataFn on_data, ErrorFn on_error) override;
  void stop() override;
  std::string description() const override;

  // Blocks until the whole file has been delivered (or stop()).
  void wait_finished();

 private:
  std::string path_;
  std::uint32_t rate_;
  double speed_;
  std::thread thread_;
  std::atomic<bool> running_{false};
  std::mutex mutex_;
  std::condition_variable cv_;
  bool finished_ = false;
};

// Sidecar metadata written next to a recording ("<capture>.meta", one
// key=value per line), so a replay can reproduce the exact tuning and the
// UTC time of the first sample.
std::map<std::string, std::string> read_capture_meta(const std::string& capture_path);
bool write_capture_meta(const std::string& capture_path, const std::map<std::string, std::string>& meta);

}  // namespace ghostnet::rx
