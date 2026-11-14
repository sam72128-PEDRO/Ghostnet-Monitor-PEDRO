#include "ghostnet/rx/rtlsdr_source.hpp"

#include <cstdio>

#include "rtl-sdr.h"

namespace ghostnet::rx {

namespace {

// 16 KiB per USB transfer = ~34 ms at 240 kS/s: small enough for good
// timestamp resolution, large enough to keep USB overhead low. 32 transfers
// in flight gives ~1 s of slack against scheduling hiccups.
constexpr std::uint32_t kBufferBytes = 16 * 1024;
constexpr std::uint32_t kBufferCount = 32;

const char* tuner_type_name(enum rtlsdr_tuner t) {
  switch (t) {
    case RTLSDR_TUNER_E4000: return "E4000";
    case RTLSDR_TUNER_FC0012: return "FC0012";
    case RTLSDR_TUNER_FC0013: return "FC0013";
    case RTLSDR_TUNER_FC2580: return "FC2580";
    case RTLSDR_TUNER_R820T: return "R820T/R860";
    case RTLSDR_TUNER_R828D: return "R828D";
    default: return "unknown";
  }
}

bool fail(std::string* error, const std::string& what, int code) {
  if (error) *error = what + " (librtlsdr error " + std::to_string(code) + ")";
  return false;
}

}  // namespace

RtlSdrSource::RtlSdrSource(rtlsdr_dev* dev) : dev_(dev) {}

RtlSdrSource::~RtlSdrSource() {
  stop();
  if (dev_) rtlsdr_close(dev_);
}

std::unique_ptr<RtlSdrSource> RtlSdrSource::open_index(std::uint32_t index, std::string* error) {
  rtlsdr_dev_t* dev = nullptr;
  const int r = rtlsdr_open(&dev, index);
  if (r < 0 || !dev) {
    if (error) *error = "could not open RTL-SDR #" + std::to_string(index) + " (error " + std::to_string(r) + ")";
    return nullptr;
  }
  return std::unique_ptr<RtlSdrSource>(new RtlSdrSource(dev));
}

std::unique_ptr<RtlSdrSource> RtlSdrSource::open_fd(int fd, std::string* error) {
  rtlsdr_dev_t* dev = nullptr;
  const int r = rtlsdr_open_sys_dev(&dev, static_cast<intptr_t>(fd));
  if (r < 0 || !dev) {
    if (error) *error = "could not open RTL-SDR from USB file descriptor (error " + std::to_string(r) + ")";
    return nullptr;
  }
  return std::unique_ptr<RtlSdrSource>(new RtlSdrSource(dev));
}

std::uint32_t RtlSdrSource::device_count() { return rtlsdr_get_device_count(); }

std::string RtlSdrSource::device_name(std::uint32_t index) {
  const char* n = rtlsdr_get_device_name(index);
  return n ? n : "";
}

bool RtlSdrSource::configure(const RtlSdrSettings& s, std::string* error) {
  if (running_) {
    if (error) *error = "cannot reconfigure while streaming";
    return false;
  }
  settings_ = s;
  int r = rtlsdr_set_sample_rate(dev_, s.sample_rate_hz);
  if (r < 0) return fail(error, "set sample rate " + std::to_string(s.sample_rate_hz), r);

  if (s.direct_sampling >= 0) {
    r = rtlsdr_set_direct_sampling(dev_, s.direct_sampling);
    if (r < 0) return fail(error, "set direct sampling", r);
  }

  r = rtlsdr_set_center_freq(dev_, s.center_hz);
  if (r < 0) return fail(error, "tune to " + std::to_string(s.center_hz) + " Hz", r);

  r = rtlsdr_set_freq_correction(dev_, s.ppm);
  if (r < 0 && r != -2) return fail(error, "set frequency correction", r);  // -2: already at that value

  if (s.tuner_gain_tenths_db < 0) {
    rtlsdr_set_tuner_gain_mode(dev_, 0);
  } else {
    rtlsdr_set_tuner_gain_mode(dev_, 1);
    rtlsdr_set_tuner_gain(dev_, s.tuner_gain_tenths_db);
  }

  r = rtlsdr_set_agc_mode(dev_, s.rtl_agc ? 1 : 0);
  if (r < 0) return fail(error, "set RTL AGC", r);

  // Never power an antenna we know nothing about.
  rtlsdr_set_bias_tee(dev_, 0);
  return true;
}

void RtlSdrSource::on_async(unsigned char* buf, std::uint32_t len, void* ctx) {
  auto* self = static_cast<RtlSdrSource*>(ctx);
  if (self->running_ && self->on_data_) self->on_data_(buf, len);
}

bool RtlSdrSource::start(DataFn on_data, ErrorFn on_error) {
  if (running_) return false;
  on_data_ = std::move(on_data);
  on_error_ = std::move(on_error);
  rtlsdr_reset_buffer(dev_);
  running_ = true;
  thread_ = std::thread([this] {
    const int r = rtlsdr_read_async(dev_, &RtlSdrSource::on_async, this, kBufferCount, kBufferBytes);
    // read_async only returns on cancel (stop()) or on a USB failure such as
    // the dongle being unplugged.
    if (running_.exchange(false) && on_error_) {
      on_error_("RTL-SDR stream ended unexpectedly (error " + std::to_string(r) +
                "); the receiver may have been unplugged or lost power");
    }
  });
  return true;
}

void RtlSdrSource::stop() {
  if (thread_.joinable()) {
    if (running_.exchange(false)) rtlsdr_cancel_async(dev_);
    thread_.join();
  }
}

bool RtlSdrSource::retune(std::uint32_t center_hz) {
  // The rtl-sdr-blog driver switches between direct sampling (below 24 MHz)
  // and the tuner by itself inside set_center_freq.
  settings_.center_hz = center_hz;
  return rtlsdr_set_center_freq(dev_, center_hz) == 0;
}

int RtlSdrSource::direct_sampling_mode() const { return rtlsdr_get_direct_sampling(dev_); }
std::uint32_t RtlSdrSource::actual_center_hz() const { return rtlsdr_get_center_freq(dev_); }
std::uint32_t RtlSdrSource::actual_sample_rate_hz() const { return rtlsdr_get_sample_rate(dev_); }
std::string RtlSdrSource::tuner_name() const { return tuner_type_name(rtlsdr_get_tuner_type(dev_)); }

std::string RtlSdrSource::description() const {
  char manufact[256] = {0};
  char product[256] = {0};
  char serial[256] = {0};
  rtlsdr_get_usb_strings(dev_, manufact, product, serial);
  std::string d = std::string(manufact) + " " + product;
  if (serial[0]) d += " (SN " + std::string(serial) + ")";
  d += ", tuner " + tuner_name();
  return d;
}

}  // namespace ghostnet::rx
