#include "ghostnet/rx/file_iq_source.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <vector>

namespace ghostnet::rx {

FileIqSource::FileIqSource(std::string path, std::uint32_t sample_rate_hz, double speed)
    : path_(std::move(path)), rate_(sample_rate_hz), speed_(speed) {}

FileIqSource::~FileIqSource() { stop(); }

bool FileIqSource::start(DataFn on_data, ErrorFn on_error) {
  std::FILE* f = std::fopen(path_.c_str(), "rb");
  if (!f) {
    if (on_error) on_error("cannot open " + path_);
    return false;
  }
  running_ = true;
  finished_ = false;
  thread_ = std::thread([this, f, on_data = std::move(on_data)] {
    std::vector<std::uint8_t> buf(16 * 1024);
    std::uint64_t delivered = 0;
    const auto t0 = std::chrono::steady_clock::now();
    while (running_) {
      const std::size_t n = std::fread(buf.data(), 1, buf.size(), f);
      if (n == 0) break;
      on_data(buf.data(), n);
      delivered += n;
      if (speed_ > 0.0) {
        const double t = static_cast<double>(delivered) / 2.0 / rate_ / speed_;
        std::this_thread::sleep_until(t0 + std::chrono::duration<double>(t));
      }
    }
    std::fclose(f);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      finished_ = true;
    }
    cv_.notify_all();
  });
  return true;
}

void FileIqSource::stop() {
  running_ = false;
  if (thread_.joinable()) thread_.join();
}

void FileIqSource::wait_finished() {
  std::unique_lock<std::mutex> lock(mutex_);
  cv_.wait(lock, [this] { return finished_ || !running_; });
}

std::string FileIqSource::description() const { return "IQ file " + path_; }

std::map<std::string, std::string> read_capture_meta(const std::string& capture_path) {
  std::map<std::string, std::string> meta;
  std::ifstream in(capture_path + ".meta");
  std::string line;
  while (std::getline(in, line)) {
    const auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    meta[line.substr(0, eq)] = line.substr(eq + 1);
  }
  return meta;
}

bool write_capture_meta(const std::string& capture_path, const std::map<std::string, std::string>& meta) {
  std::ofstream out(capture_path + ".meta");
  for (const auto& [k, v] : meta) out << k << '=' << v << '\n';
  return static_cast<bool>(out);
}

}  // namespace ghostnet::rx
