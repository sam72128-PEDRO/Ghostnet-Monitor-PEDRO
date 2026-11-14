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
| `MainActivity.kt` | Compose host; USB permission and start/stop |
| `ui/` | The four tabs (Receive, Schedule, Export, Settings), waterfall, status logic |
| `MonitorService.kt` | Foreground service: USB connection, native receiver, wake lock, session lifecycle, recording, DB writes, diagnostics events |
| `NativeReceiver.kt` | JNI surface (open fd, start, stop, stats, spectrum, self-test, native log). No transmit methods. |
| `GhostNetPlan.kt`, `ScheduleEngine.kt` | GhostNet V1.5 frequency catalog and UTC schedule with local-time conversion |
| `SessionDatabase.kt`, `SessionExporter.kt` | sessions / frames / messages, retention, JSON / CSV / TXT export |
| `DiagLog.kt`, `DiagnosticsExporter.kt`, `GhostNetApp.kt` | Diagnostics log, crash and exit-reason capture, diagnostics report |
| `Recordings.kt`, `Waterfall.kt` | Raw IQ recordings; waterfall image buffer |
| `src/main/cpp/ghostnet_jni.cpp` | JNI bridge into rx-core; captures librtlsdr stderr |

Tests (`./gradlew :app:testDebugUnitTest`): schedule and frequency
acceptance checks, Robolectric UI smoke tests of all four tabs, and
storage / export / retention / diagnostics tests.

How to test with real hardware: [`../TESTING.md`](../TESTING.md).
