#include "ghostnet/rx/wav_audio.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "ghostnet/rf/fir.hpp"

namespace ghostnet::rx {

namespace {

std::uint32_t le32(const unsigned char* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (std::uint32_t(p[3]) << 24); }
std::uint16_t le16(const unsigned char* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }

bool fail(std::string* error, const std::string& what) {
  if (error) *error = what;
  return false;
}

}  // namespace

bool read_wav_mono(const std::string& path, double out_rate, std::vector<float>* out, WavInfo* info,
                   std::string* error) {
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return fail(error, "cannot open " + path);
  std::vector<unsigned char> buf;
  unsigned char tmp[65536];
  for (std::size_t n; (n = std::fread(tmp, 1, sizeof tmp, f)) > 0;) buf.insert(buf.end(), tmp, tmp + n);
  std::fclose(f);
  if (buf.size() < 12 || std::memcmp(buf.data(), "RIFF", 4) != 0 || std::memcmp(buf.data() + 8, "WAVE", 4) != 0)
    return fail(error, path + " is not a RIFF/WAVE file");

  int format = 0, channels = 0, rate = 0, bits = 0;
  const unsigned char* data = nullptr;
  std::size_t data_len = 0;
  for (std::size_t pos = 12; pos + 8 <= buf.size();) {
    const std::uint32_t len = le32(&buf[pos + 4]);
    const unsigned char* body = &buf[pos + 8];
    const std::size_t avail = std::min<std::size_t>(len, buf.size() - pos - 8);
    if (std::memcmp(&buf[pos], "fmt ", 4) == 0 && avail >= 16) {
      format = le16(body);
      channels = le16(body + 2);
      rate = static_cast<int>(le32(body + 4));
      bits = le16(body + 14);
      if (format == 0xFFFE && avail >= 26) format = le16(body + 24);  // WAVE_FORMAT_EXTENSIBLE sub-format
    } else if (std::memcmp(&buf[pos], "data", 4) == 0) {
      data = body;
      data_len = avail;
    }
    pos += 8 + len + (len & 1);
  }
  if (!data || channels <= 0 || rate <= 0) return fail(error, path + ": missing fmt or data chunk");
  if (!(format == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) && !(format == 3 && bits == 32))
    return fail(error, path + ": unsupported WAV encoding (format " + std::to_string(format) + ", " +
                           std::to_string(bits) + " bits); convert to 16-bit PCM");

  const std::size_t bytes = static_cast<std::size_t>(bits / 8);
  const std::size_t frames = data_len / (bytes * static_cast<std::size_t>(channels));
  std::vector<float> mono(frames);
  for (std::size_t i = 0; i < frames; ++i) {
    double sum = 0.0;
    for (int c = 0; c < channels; ++c) {
      const unsigned char* p = data + (i * static_cast<std::size_t>(channels) + static_cast<std::size_t>(c)) * bytes;
      double v = 0.0;
      if (format == 3) {
        float x;
        std::memcpy(&x, p, 4);
        v = x;
      } else if (bits == 8) {
        v = (p[0] - 128) / 128.0;
      } else if (bits == 16) {
        v = static_cast<std::int16_t>(le16(p)) / 32768.0;
      } else if (bits == 24) {
        std::int32_t x = p[0] | (p[1] << 8) | (p[2] << 16);
        if (x & 0x800000) x |= ~0xFFFFFF;
        v = x / 8388608.0;
      } else {
        v = static_cast<std::int32_t>(le32(p)) / 2147483648.0;
      }
      sum += v;
    }
    mono[i] = static_cast<float>(sum / channels);
  }

  // Anti-alias, then linear interpolation to the output rate.
  if (out_rate < rate) {
    const auto taps = rf::design_lowpass_fir(127, 0.45 * out_rate, rate);
    std::vector<float> filtered(mono.size(), 0.0f);
    const std::size_t half = taps.size() / 2;
    for (std::size_t i = 0; i < mono.size(); ++i) {
      double acc = 0.0;
      for (std::size_t k = 0; k < taps.size(); ++k) {
        const std::ptrdiff_t j = static_cast<std::ptrdiff_t>(i + half) - static_cast<std::ptrdiff_t>(k);
        if (j >= 0 && j < static_cast<std::ptrdiff_t>(mono.size())) acc += taps[k] * mono[static_cast<std::size_t>(j)];
      }
      filtered[i] = static_cast<float>(acc);
    }
    mono.swap(filtered);
  }
  out->clear();
  const double step = static_cast<double>(rate) / out_rate;
  for (double t = 0.0; t + 1.0 < static_cast<double>(mono.size()); t += step) {
    const auto i = static_cast<std::size_t>(t);
    const double a = t - static_cast<double>(i);
    out->push_back(static_cast<float>((1.0 - a) * mono[i] + a * mono[i + 1]));
  }
  if (info) {
    info->sample_rate_hz = rate;
    info->channels = channels;
    info->bits = bits;
    info->seconds = static_cast<double>(frames) / rate;
  }
  return true;
}

bool write_wav_mono16(const std::string& path, const std::vector<float>& samples, int rate) {
  std::FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  auto put32 = [f](std::uint32_t v) { unsigned char b[4] = {static_cast<unsigned char>(v), static_cast<unsigned char>(v >> 8), static_cast<unsigned char>(v >> 16), static_cast<unsigned char>(v >> 24)}; std::fwrite(b, 1, 4, f); };
  auto put16 = [f](std::uint16_t v) { unsigned char b[2] = {static_cast<unsigned char>(v), static_cast<unsigned char>(v >> 8)}; std::fwrite(b, 1, 2, f); };
  const std::uint32_t data_len = static_cast<std::uint32_t>(samples.size() * 2);
  std::fwrite("RIFF", 1, 4, f);
  put32(36 + data_len);
  std::fwrite("WAVEfmt ", 1, 8, f);
  put32(16);
  put16(1);
  put16(1);
  put32(static_cast<std::uint32_t>(rate));
  put32(static_cast<std::uint32_t>(rate * 2));
  put16(2);
  put16(16);
  std::fwrite("data", 1, 4, f);
  put32(data_len);
  for (float s : samples) put16(static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(std::clamp(s, -1.0f, 1.0f) * 32767.0f))));
  return std::fclose(f) == 0;
}

}  // namespace ghostnet::rx
