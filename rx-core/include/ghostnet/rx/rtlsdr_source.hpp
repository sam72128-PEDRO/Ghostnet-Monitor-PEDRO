#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

#include "ghostnet/rx/iq_source.hpp"

struct rtlsdr_dev;

namespace ghostnet::rx {

struct RtlSdrSettings {
  std::uint32_t center_hz = 0;
  std::uint32_t sample_rate_hz = 240000;
  int ppm = 0;

  // RTL2832U digital AGC. Usually helps in direct-sampling mode, where the
  // R820T tuner (and its gain stages) is bypassed entirely.
  bool rtl_agc = true;

  // Tuner gain in tenths of a dB, -1 for tuner AGC. Only meaningful above
  // ~24 MHz where the tuner path is used (not for the GhostNet HF presets).
  int tuner_gain_tenths_db = -1;

  // -1: let the RTL-SDR Blog driver choose (it switches the V3 to Q-branch
  // direct sampling below 24 MHz). 0: off, 1: I-branch, 2: Q-branch.
  int direct_sampling = -1;
};

// Receive-only IqSource over librtlsdr (rtl-sdr-blog fork). Nothing in this
// class can transmit: RTL-SDR hardware has no transmitter, and the only
// outputs it drives are tuner/demod registers and, explicitly switched off
// here, the bias-tee supply.
class RtlSdrSource : public IqSource {
 public:
  ~RtlSdrSource() override;

  // Desktop: open the index-th RTL-SDR found by enumeration.
  static std::unique_ptr<RtlSdrSource> open_index(std::uint32_t index, std::string* error);

  // Android: open from UsbDeviceConnection.getFileDescriptor(). The caller
  // keeps the UsbDeviceConnection open until this object is destroyed.
  static std::unique_ptr<RtlSdrSource> open_fd(int fd, std::string* error);

  static std::uint32_t device_count();
  static std::string device_name(std::uint32_t index);

  bool configure(const RtlSdrSettings& settings, std::string* error);

  bool start(DataFn on_data, ErrorFn on_error) override;
  void stop() override;
  bool retune(std::uint32_t center_hz) override;
  std::string last_retune_detail() const override { return retune_detail_; }
  std::string description() const override;

  // After configure(): what the driver actually ended up doing.
  int direct_sampling_mode() const;
  std::uint32_t actual_center_hz() const;
  std::uint32_t actual_sample_rate_hz() const;
  std::string tuner_name() const;

 private:
  explicit RtlSdrSource(rtlsdr_dev* dev);
  static void on_async(unsigned char* buf, std::uint32_t len, void* ctx);

  rtlsdr_dev* dev_;
  std::thread thread_;
  std::atomic<bool> running_{false};
  DataFn on_data_;
  ErrorFn on_error_;
  RtlSdrSettings settings_;
  std::string retune_detail_;
};

}  // namespace ghostnet::rx
