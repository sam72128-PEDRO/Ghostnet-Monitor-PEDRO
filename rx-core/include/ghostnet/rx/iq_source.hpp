#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace ghostnet::rx {

// Anything that produces RTL2832U-format IQ: interleaved unsigned 8-bit
// I,Q pairs at a fixed sample rate. Receive-only by construction — there
// is no method here, or on any implementation, that sends anything to a
// radio.
class IqSource {
 public:
  using DataFn = std::function<void(const std::uint8_t* iq, std::size_t bytes)>;
  using ErrorFn = std::function<void(const std::string& message)>;

  virtual ~IqSource() = default;

  // Begin delivering data on a source-owned thread. on_data must not be
  // called after stop() returns. on_error reports a fatal condition (device
  // unplugged, read failure); the source stops delivering after it.
  virtual bool start(DataFn on_data, ErrorFn on_error) = 0;
  virtual void stop() = 0;

  // Retune while streaming (scanning). Returns false if unsupported or the
  // device refused. Data after a successful return is from the new
  // frequency, apart from what was already in flight in USB buffers.
  virtual bool retune(std::uint32_t center_hz) {
    (void)center_hz;
    return false;
  }

  // What the last retune() actually did (e.g. "centre 7129500 Hz, direct
  // sampling 2"), or why it failed. For the diagnostics log.
  virtual std::string last_retune_detail() const { return {}; }

  virtual std::string description() const = 0;
};

}  // namespace ghostnet::rx
