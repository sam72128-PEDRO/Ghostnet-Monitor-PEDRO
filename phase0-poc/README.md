# GhostNet Phase 0 Android harness

This is deliberately a small receive-only build target. It contains the JS8
native decoder core, a JNI PCM entry point, and an Android USB-host probe. It
has no microphone, network, transmit, PTT, Hamlib, or radio-control permissions.

When launched on Android, it lists USB devices, displays VID/PID and interface
descriptors, and requests permission for the first device. It does not yet
claim the RTL-SDR bulk endpoints or capture IQ.

Build with Java 17 and the pinned Android toolchain:

```bash
JAVA_HOME=/opt/homebrew/opt/openjdk@17/libexec/openjdk.jdk/Contents/Home \
ANDROID_HOME=/Users/samuelkinard/Library/Android/sdk \
./gradlew :app:assembleDebug
```

The current build produces `app/build/outputs/apk/debug/app-debug.apk` for
`arm64-v8a`. Runtime decoder and USB tests still require an Android device.
