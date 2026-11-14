// JNI bridge between org.ghostnet.monitor.NativeReceiver and rx-core.
//
// Receive-only: the only operations exposed are open (from a UsbManager file
// descriptor), start/stop receiving, read statistics, and the offline
// self-test. There is no transmit, PTT, CAT or audio-output entry point.

#include <android/log.h>
#include <jni.h>

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "ghostnet/rx/receiver.hpp"
#include "ghostnet/rx/rtlsdr_source.hpp"
#include "ghostnet/rx/test_signal.hpp"

using namespace ghostnet::rx;

namespace {

constexpr char kTag[] = "GhostNetNative";
JavaVM* g_vm = nullptr;

// Native threads created by js8core/librtlsdr call back into Java. Each is
// attached on first use and detached automatically when the thread exits
// (Android aborts the process if an attached thread exits without
// detaching).
struct ThreadAttachment {
  JNIEnv* env = nullptr;
  bool attached_here = false;
  ~ThreadAttachment() {
    if (attached_here && g_vm) g_vm->DetachCurrentThread();
  }
};

JNIEnv* env_for_this_thread() {
  thread_local ThreadAttachment attachment;
  if (attachment.env) return attachment.env;
  if (g_vm->GetEnv(reinterpret_cast<void**>(&attachment.env), JNI_VERSION_1_6) == JNI_OK) return attachment.env;
  if (g_vm->AttachCurrentThread(&attachment.env, nullptr) != JNI_OK) {
    attachment.env = nullptr;
    return nullptr;
  }
  attachment.attached_here = true;
  return attachment.env;
}

// NewStringUTF requires valid modified UTF-8 and aborts under CheckJNI
// otherwise. Decoded radio text is untrusted, so keep it to printable ASCII.
jstring jstr(JNIEnv* env, const std::string& s) {
  std::string clean;
  clean.reserve(s.size());
  for (unsigned char c : s) clean.push_back((c >= 0x20 && c < 0x7f) ? static_cast<char>(c) : '?');
  return env->NewStringUTF(clean.c_str());
}

void clear_exception(JNIEnv* env, const char* where) {
  if (env->ExceptionCheck()) {
    __android_log_print(ANDROID_LOG_ERROR, kTag, "Java exception in %s callback", where);
    env->ExceptionDescribe();
    env->ExceptionClear();
  }
}

struct Session {
  std::mutex mutex;
  std::unique_ptr<RtlSdrSource> source;
  std::unique_ptr<Receiver> receiver;
  jobject listener = nullptr;  // global ref while receiving
  jmethodID on_frame = nullptr;
  jmethodID on_message = nullptr;
  jmethodID on_source_error = nullptr;
  std::string description;
};

Session* session(jlong handle) { return reinterpret_cast<Session*>(handle); }

void release_listener(JNIEnv* env, Session* s) {
  if (s->listener) {
    env->DeleteGlobalRef(s->listener);
    s->listener = nullptr;
  }
}

}  // namespace

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void*) {
  g_vm = vm;
  return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT jlong JNICALL Java_org_ghostnet_monitor_NativeReceiver_nativeCreate(JNIEnv*, jclass) {
  return reinterpret_cast<jlong>(new Session());
}

extern "C" JNIEXPORT void JNICALL Java_org_ghostnet_monitor_NativeReceiver_nativeDestroy(JNIEnv* env, jclass,
                                                                                          jlong handle) {
  Session* s = session(handle);
  if (!s) return;
  {
    std::lock_guard<std::mutex> lock(s->mutex);
    if (s->receiver) s->receiver->stop();
    s->receiver.reset();
    s->source.reset();  // rtlsdr_close(); must happen before Java closes the USB connection
    release_listener(env, s);
  }
  delete s;
}

// Returns null on success, or an error message.
extern "C" JNIEXPORT jstring JNICALL Java_org_ghostnet_monitor_NativeReceiver_nativeOpenFd(JNIEnv* env, jclass,
                                                                                          jlong handle, jint fd) {
  Session* s = session(handle);
  std::lock_guard<std::mutex> lock(s->mutex);
  if (s->receiver) return jstr(env, "already receiving");
  std::string error;
  s->source = RtlSdrSource::open_fd(fd, &error);
  if (!s->source) return jstr(env, error);
  s->description = s->source->description();
  __android_log_print(ANDROID_LOG_INFO, kTag, "opened %s", s->description.c_str());
  return nullptr;
}

// Returns null on success, or an error message.
extern "C" JNIEXPORT jstring JNICALL Java_org_ghostnet_monitor_NativeReceiver_nativeStart(
    JNIEnv* env, jclass, jlong handle, jint dial_hz, jint sample_rate_hz, jint capture_offset_hz, jboolean usb,
    jboolean invert_spectrum, jint ppm, jboolean rtl_agc, jint direct_sampling, jint submode_mask, jobject listener) {
  Session* s = session(handle);
  std::lock_guard<std::mutex> lock(s->mutex);
  if (!s->source) return jstr(env, "no receiver open");
  if (s->receiver) return jstr(env, "already receiving");

  ReceiverConfig cfg;
  cfg.dial_hz = static_cast<std::uint32_t>(dial_hz);
  cfg.sample_rate_hz = static_cast<std::uint32_t>(sample_rate_hz);
  cfg.capture_offset_hz = capture_offset_hz;
  cfg.sideband = usb ? ghostnet::rf::Sideband::kUsb : ghostnet::rf::Sideband::kLsb;
  cfg.invert_spectrum = invert_spectrum;
  cfg.submode_mask = submode_mask;

  RtlSdrSettings rtl;
  rtl.center_hz = capture_center_hz(cfg);
  rtl.sample_rate_hz = cfg.sample_rate_hz;
  rtl.ppm = ppm;
  rtl.rtl_agc = rtl_agc;
  rtl.direct_sampling = direct_sampling;
  std::string error;
  if (!s->source->configure(rtl, &error)) return jstr(env, "configure failed: " + error);

  release_listener(env, s);
  s->listener = env->NewGlobalRef(listener);
  jclass cls = env->GetObjectClass(listener);
  s->on_frame = env->GetMethodID(
      cls, "onFrame",
      "(JIDIFILjava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V");
  s->on_message = env->GetMethodID(
      cls, "onMessage",
      "(JJIDIIIZZZZLjava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V");
  s->on_source_error = env->GetMethodID(cls, "onSourceError", "(Ljava/lang/String;)V");
  env->DeleteLocalRef(cls);
  if (!s->on_frame || !s->on_message || !s->on_source_error) {
    env->ExceptionClear();
    release_listener(env, s);
    return jstr(env, "listener is missing a callback method");
  }

  ReceiverCallbacks cb;
  cb.on_frame = [s](const FrameEvent& f) {
    JNIEnv* e = env_for_this_thread();
    if (!e || !s->listener) return;
    jstring kind = jstr(e, frame_kind_name(f.frame.kind));
    jstring raw = jstr(e, f.frame.raw);
    jstring text = jstr(e, f.frame.text);
    jstring from = jstr(e, f.frame.from);
    jstring to = jstr(e, f.frame.to);
    e->CallVoidMethod(s->listener, s->on_frame, static_cast<jlong>(f.utc_ms), static_cast<jint>(f.submode),
                      static_cast<jdouble>(f.offset_hz), static_cast<jint>(f.snr_db), static_cast<jfloat>(f.dt_s),
                      static_cast<jint>(f.frame.flags), kind, raw, text, from, to);
    clear_exception(e, "onFrame");
    for (jstring j : {kind, raw, text, from, to}) e->DeleteLocalRef(j);
  };
  cb.on_message = [s](const AssembledMessage& m) {
    JNIEnv* e = env_for_this_thread();
    if (!e || !s->listener) return;
    std::string raw_frames;
    for (const auto& f : m.frames) {
      if (!raw_frames.empty()) raw_frames.push_back(' ');
      raw_frames += f.frame.raw;
    }
    jstring text = jstr(e, m.text);
    jstring from = jstr(e, m.from);
    jstring to = jstr(e, m.to);
    jstring grid = jstr(e, m.grid);
    jstring raws = jstr(e, raw_frames);
    e->CallVoidMethod(s->listener, s->on_message, static_cast<jlong>(m.first_utc_ms),
                      static_cast<jlong>(m.last_utc_ms), static_cast<jint>(m.submode),
                      static_cast<jdouble>(m.offset_hz), static_cast<jint>(m.snr_min_db),
                      static_cast<jint>(m.snr_max_db), static_cast<jint>(m.frames.size()),
                      static_cast<jboolean>(m.complete), static_cast<jboolean>(m.missing_start),
                      static_cast<jboolean>(m.has_gap), static_cast<jboolean>(m.timed_out), text, from, to, grid,
                      raws);
    clear_exception(e, "onMessage");
    for (jstring j : {text, from, to, grid, raws}) e->DeleteLocalRef(j);
  };
  cb.on_source_error = [s](const std::string& message) {
    JNIEnv* e = env_for_this_thread();
    if (!e || !s->listener) return;
    jstring msg = jstr(e, message);
    e->CallVoidMethod(s->listener, s->on_source_error, msg);
    clear_exception(e, "onSourceError");
    e->DeleteLocalRef(msg);
  };
  cb.on_log = [](const std::string& line) { __android_log_print(ANDROID_LOG_INFO, kTag, "%s", line.c_str()); };

  s->receiver = std::make_unique<Receiver>(cfg, cb);
  if (!s->receiver->start(s->source.get())) {
    s->receiver.reset();
    release_listener(env, s);
    return jstr(env, "receiver failed to start");
  }
  s->description = s->source->description() + ", centre " + std::to_string(s->source->actual_center_hz()) +
                   " Hz, " + std::to_string(s->source->actual_sample_rate_hz()) + " S/s, direct sampling " +
                   std::to_string(s->source->direct_sampling_mode());
  __android_log_print(ANDROID_LOG_INFO, kTag, "receiving: %s", s->description.c_str());
  return nullptr;
}

// Blocks until the source and decoder have stopped; any half-received
// messages are delivered (as incomplete) before this returns.
extern "C" JNIEXPORT void JNICALL Java_org_ghostnet_monitor_NativeReceiver_nativeStop(JNIEnv* env, jclass,
                                                                                       jlong handle) {
  Session* s = session(handle);
  std::lock_guard<std::mutex> lock(s->mutex);
  if (s->receiver) s->receiver->stop();
  s->receiver.reset();
  release_listener(env, s);
}

extern "C" JNIEXPORT jstring JNICALL Java_org_ghostnet_monitor_NativeReceiver_nativeDescribe(JNIEnv* env, jclass,
                                                                                            jlong handle) {
  Session* s = session(handle);
  std::lock_guard<std::mutex> lock(s->mutex);
  return jstr(env, s->description);
}

// Layout must match NativeReceiver.Stats.fromArray().
extern "C" JNIEXPORT jdoubleArray JNICALL Java_org_ghostnet_monitor_NativeReceiver_nativeStats(JNIEnv* env, jclass,
                                                                                              jlong handle) {
  Session* s = session(handle);
  ReceiverStats st;
  {
    std::lock_guard<std::mutex> lock(s->mutex);
    if (s->receiver) st = s->receiver->stats();
  }
  const std::vector<double> v = {st.running ? 1.0 : 0.0,
                                 static_cast<double>(st.iq_bytes),
                                 static_cast<double>(st.iq_bytes_dropped),
                                 st.iq_level_dbfs,
                                 st.iq_clip_percent,
                                 st.audio_level_dbfs,
                                 st.agc_gain_db,
                                 static_cast<double>(st.decode_passes),
                                 static_cast<double>(st.frames_decoded),
                                 static_cast<double>(st.messages),
                                 st.median_dt_s,
                                 static_cast<double>(st.dt_samples),
                                 static_cast<double>(st.last_iq_unix_ms),
                                 static_cast<double>(st.last_frame_unix_ms),
                                 st.spectrum_power_db,
                                 st.spectrum_peak_db};
  jdoubleArray out = env->NewDoubleArray(static_cast<jsize>(v.size()));
  env->SetDoubleArrayRegion(out, 0, static_cast<jsize>(v.size()), v.data());
  return out;
}

// Layout must match NativeReceiver.SelfTestResult.fromArray().
extern "C" JNIEXPORT jobjectArray JNICALL Java_org_ghostnet_monitor_NativeReceiver_nativeSelfTest(JNIEnv* env, jclass,
                                                                                                 jdouble speed) {
  const SelfTestResult r = run_self_test(speed);
  const std::vector<std::string> v = {r.passed ? "1" : "0",
                                      r.expected_text,
                                      r.decoded_text,
                                      std::to_string(r.frames_decoded),
                                      std::to_string(r.frames_expected),
                                      std::to_string(r.capture_seconds),
                                      std::to_string(r.wall_seconds),
                                      r.detail};
  jclass string_class = env->FindClass("java/lang/String");
  jobjectArray out = env->NewObjectArray(static_cast<jsize>(v.size()), string_class, nullptr);
  for (std::size_t i = 0; i < v.size(); ++i) {
    jstring j = jstr(env, v[i]);
    env->SetObjectArrayElement(out, static_cast<jsize>(i), j);
    env->DeleteLocalRef(j);
  }
  return out;
}
