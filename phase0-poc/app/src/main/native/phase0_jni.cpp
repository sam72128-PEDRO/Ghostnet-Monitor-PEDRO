#include <jni.h>
#include <android/log.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "js8core/decoder.hpp"
#include "js8core/engine.hpp"
#include "js8core/protocol/constants.hpp"

namespace {
constexpr char kTag[] = "GhostNetPhase0";
}

extern "C" JNIEXPORT jint JNICALL
Java_org_ghostnet_phase0_MainActivity_nativeDecodePcm(
    JNIEnv* env, jobject, jshortArray pcm) {
  const jsize count = env->GetArrayLength(pcm);
  std::vector<jshort> input(static_cast<size_t>(count));
  env->GetShortArrayRegion(pcm, 0, count, input.data());

  js8core::DecodeState state;
  state.samples.assign(static_cast<size_t>(js8core::protocol::kJs8NtMax *
                                           js8core::protocol::kJs8RxSampleRate), 0);
  const size_t copied = std::min(input.size(), state.samples.size());
  std::copy(input.begin(), input.begin() + copied, state.samples.begin());
  state.params.nfa = 200;
  state.params.nfb = 2500;
  state.params.nfqso = 1500;
  state.params.nsubmodes = 1;
  state.params.kposA = 0;
  state.params.kszA = 15 * js8core::protocol::kJs8RxSampleRate;

  int decoded = 0;
  js8core::legacy_decode(state, [&](js8core::events::Variant const& event) {
    if (auto const* message = std::get_if<js8core::events::Decoded>(&event)) {
      ++decoded;
      __android_log_print(ANDROID_LOG_INFO, kTag, "decoded: %s", message->data.c_str());
    }
  });
  return decoded;
}
