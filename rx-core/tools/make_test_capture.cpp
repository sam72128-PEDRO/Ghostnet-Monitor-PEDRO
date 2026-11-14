// Writes a synthetic RTL-SDR capture (.cu8 + .cu8.meta) containing a JS8
// transmission of a typed message, for exercising `ghostnet-rx --iq-file`
// or any other consumer of rtl_sdr-format IQ without a radio.
//
//   make-test-capture --out test.cu8 [--call N0CALL] [--text "@GHOSTNET HELLO"]
//                     [--noise 6] [--signal 3] [--audio-hz 1500] [--mirrored]
//
// Host-only test tool; never built into the Android app.

#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

#include "ghostnet/rx/file_iq_source.hpp"
#include "ghostnet/rx/test_signal.hpp"
#include "js8core/decoder.hpp"
#include "js8core/protocol/costas.hpp"
#include "js8core/protocol/varicode.hpp"

using namespace ghostnet::rx;

int main(int argc, char** argv) {
  std::string out;
  std::string call = "N0CALL";
  std::string text = "@GHOSTNET TEST CAPTURE";
  TestSignalParams p;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
    if (a == "--out") out = next();
    else if (a == "--call") call = next();
    else if (a == "--text") text = next();
    else if (a == "--noise") p.noise_counts = std::stod(next());
    else if (a == "--signal") p.signal_counts = std::stod(next());
    else if (a == "--audio-hz") p.audio_hz = std::stod(next());
    else if (a == "--mirrored") p.mirrored = true;
    else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
  }
  if (out.empty()) {
    std::fprintf(stderr, "usage: make-test-capture --out FILE.cu8 [--call C] [--text T] [--noise N] [--signal S]\n");
    return 2;
  }

  auto built = js8core::protocol::varicode::build_message_frames(call, "", "", text, false, false, 0, nullptr);
  std::vector<Js8Tones> tones(built.size());
  for (std::size_t f = 0; f < built.size(); ++f) {
    js8core::legacy_encode(built[f].second, js8core::protocol::costas(js8core::protocol::CostasType::Original),
                           built[f].first.c_str(), tones[f].data());
  }
  const auto sig = synthesize_test_signal(tones, p);

  std::FILE* f = std::fopen(out.c_str(), "wb");
  if (!f) { std::perror(out.c_str()); return 1; }
  std::fwrite(sig.iq.data(), 1, sig.iq.size(), f);
  std::fclose(f);
  write_capture_meta(out, {{"dial_hz", "7107000"},
                           {"center_hz", std::to_string(7107000 + p.capture_offset_hz)},
                           {"capture_offset_hz", std::to_string(p.capture_offset_hz)},
                           {"sample_rate_hz", std::to_string(p.sample_rate_hz)},
                           {"start_unix_ms", std::to_string(sig.start_unix_ms)},
                           {"format", "cu8"},
                           {"synthetic", "1"}});
  std::printf("wrote %s (%zu frames, %.1f s, %zu bytes)\n", out.c_str(), built.size(),
              sig.iq.size() / 2.0 / p.sample_rate_hz, sig.iq.size());
  return 0;
}
