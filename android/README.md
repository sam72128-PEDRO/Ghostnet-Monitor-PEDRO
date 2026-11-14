# GhostNet Monitor — Android app

Receive-only JS8 monitor for an RTL-SDR Blog V3 on the Galaxy Tab S7 FE.
The native receive chain lives in `../rx-core` and `../rf-frontend` and is
built into `libghostnet_native.so` through `app/CMakeLists.txt`.

```bash
# JDK 17, Android SDK platform 35, NDK 25.1.8937393, CMake 3.22.1
echo "sdk.dir=/path/to/Android/sdk" > local.properties
./gradlew :app:assembleDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

Output: `app/build/outputs/apk/debug/app-debug.apk` (arm64-v8a only).
The native code is built at `-O2` even in debug builds, because the DSP has
to keep up with 240 kS/s.

| File | Role |
|---|---|
| `MainActivity.kt` | Presets, start/stop, Device/Signal/Decoder/Clock status, message and frame lists, self-test, export |
| `MonitorService.kt` | Foreground service: USB connection, native receiver, wake lock, session lifecycle, DB writes |
| `NativeReceiver.kt` | JNI surface (open fd, start, stop, stats, self-test). No transmit methods. |
| `SessionDatabase.kt` | sessions / frames / messages tables, crash recovery |
| `SessionExporter.kt` | JSON export through the Storage Access Framework |
| `src/main/cpp/ghostnet_jni.cpp` | JNI bridge into rx-core |

How to test with real hardware: [`../TESTING.md`](../TESTING.md).
