package org.ghostnet.monitor

import android.app.ActivityManager
import android.app.NotificationManager
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.hardware.usb.UsbManager
import android.net.Uri
import android.os.BatteryManager
import android.os.Build
import android.os.PowerManager
import android.provider.Settings
import java.io.File
import java.time.Instant
import java.time.ZoneId
import java.util.Locale
import java.util.concurrent.TimeUnit

/**
 * One text file with everything needed to diagnose a problem remotely:
 * device and Android state, USB devices and permissions, settings, live
 * receiver state, recent sessions, recordings, why the app last exited
 * (including native crashes), the diagnostics log, and the app's own
 * logcat. Raw decoded frames appear in the log; nothing secret does.
 */
object DiagnosticsExporter {

    fun suggestedFileName(): String =
        "GNM_diagnostics_${java.time.format.DateTimeFormatter.ofPattern("yyyyMMdd'T'HHmmss'Z'")
            .withZone(java.time.ZoneOffset.UTC).format(Instant.now())}.txt"

    fun export(context: Context, target: Uri) {
        val text = build(context)
        context.contentResolver.openOutputStream(target, "wt").use { out ->
            requireNotNull(out) { "could not open export destination" }
            out.write(text.toByteArray(Charsets.UTF_8))
            out.flush()
        }
        DiagLog.i("diag", "diagnostics exported (${text.length} chars)")
    }

    fun build(c: Context): String = buildString {
        fun section(title: String) {
            appendLine()
            appendLine("===== $title =====")
        }
        fun safe(what: String, block: () -> Unit) = try {
            block()
        } catch (t: Throwable) {
            appendLine("($what unavailable: ${t.javaClass.simpleName}: ${t.message})")
        }

        appendLine("GhostNet Monitor diagnostics")
        appendLine("Generated: ${Instant.now()} (system clock)")
        appendLine("App: ${c.packageName} ${BuildConfigInfo.VERSION}")

        section("Device")
        appendLine("Model: ${Build.MANUFACTURER} ${Build.MODEL} (${Build.DEVICE}, ${Build.PRODUCT})")
        appendLine("Android: ${Build.VERSION.RELEASE} (API ${Build.VERSION.SDK_INT}), security patch ${Build.VERSION.SECURITY_PATCH}")
        appendLine("Build: ${Build.DISPLAY}")
        appendLine("ABIs: ${Build.SUPPORTED_ABIS.joinToString()}")
        appendLine("Time zone: ${ZoneId.systemDefault()} · locale ${Locale.getDefault()}")
        safe("auto time") {
            appendLine("Automatic date & time: " + (Settings.Global.getInt(c.contentResolver, Settings.Global.AUTO_TIME, -1).let {
                when (it) { 1 -> "on"; 0 -> "OFF (JS8 needs accurate time)"; else -> "unknown" }
            }))
        }
        safe("battery") {
            val b = c.registerReceiver(null, IntentFilter(Intent.ACTION_BATTERY_CHANGED))
            val level = b?.getIntExtra(BatteryManager.EXTRA_LEVEL, -1) ?: -1
            val scale = b?.getIntExtra(BatteryManager.EXTRA_SCALE, 100) ?: 100
            val plugged = b?.getIntExtra(BatteryManager.EXTRA_PLUGGED, 0) ?: 0
            val temp = (b?.getIntExtra(BatteryManager.EXTRA_TEMPERATURE, 0) ?: 0) / 10.0
            appendLine("Battery: ${level * 100 / scale.coerceAtLeast(1)}%, ${if (plugged != 0) "charging/plugged ($plugged)" else "on battery"}, $temp C")
            val pm = c.getSystemService(PowerManager::class.java)
            appendLine("Power save: ${pm.isPowerSaveMode} · ignoring battery optimisation: ${pm.isIgnoringBatteryOptimizations(c.packageName)}")
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) appendLine("Thermal status: ${pm.currentThermalStatus} (0 none .. 6 shutdown)")
        }
        safe("notifications") {
            appendLine("Notifications enabled: ${c.getSystemService(NotificationManager::class.java).areNotificationsEnabled()}")
        }

        section("USB devices")
        safe("USB") {
            val usb = c.getSystemService(UsbManager::class.java)
            val devices = usb.deviceList.values
            if (devices.isEmpty()) appendLine("(none attached)")
            for (d in devices) {
                appendLine(
                    "${d.deviceName}: %04x:%04x %s / %s · interfaces %d · permission %s%s".format(
                        d.vendorId, d.productId, d.manufacturerName ?: "?", d.productName ?: "?", d.interfaceCount,
                        if (usb.hasPermission(d)) "granted" else "NOT granted",
                        if (RtlSdrDevices.isRtlSdr(d)) " · recognised RTL-SDR" else "",
                    ),
                )
            }
        }

        section("Settings")
        appendLine(ReceiveSettings.load(c).describe())
        appendLine("Retention: ${AppPrefs.retentionDays(c).let { if (it == 0) "keep forever" else "$it days" }}")

        section("Receiver state now")
        appendLine("Phase: ${MonitorState.phase}${MonitorState.error?.let { " · error: $it" } ?: ""}")
        appendLine("Device: ${MonitorState.deviceText.ifEmpty { "-" }}")
        MonitorState.stats?.let { s ->
            appendLine("IQ ${s.iqBytes} bytes, dropped ${s.droppedBytes}, level %.1f dBFS, clip %.2f%%".format(s.iqLevelDbfs, s.clipPercent))
            appendLine("Channel %.1f dBFS, AGC %+.1f dB".format(s.channelLevelDbfs, s.agcGainDb))
            appendLine("Decode passes ${s.decodePasses}, frames ${s.frames}, messages ${s.messages}")
            appendLine("Median DT %+.2f s over %d decodes".format(s.medianDtS, s.dtSamples))
            appendLine("Recording: ${s.recording}, ${s.recordedBytes} bytes")
        } ?: appendLine("(not receiving)")

        section("Recent sessions")
        safe("sessions") {
            val db = SessionDatabase(c)
            val sessions = db.listSessions(20)
            db.close()
            if (sessions.isEmpty()) appendLine("(none)")
            for (s in sessions) {
                appendLine("${Instant.ofEpochMilli(s.startUtcMs)} → ${s.endUtcMs?.let { Instant.ofEpochMilli(it) } ?: "active"} · ${s.state}" +
                    "${s.endReason?.let { " ($it)" } ?: ""} · ${s.dialHz} Hz · ${s.frames} frames · ${s.messages} msgs")
                appendLine("    ${s.settings}")
                s.clockNote?.let { appendLine("    clock: $it") }
                s.notes?.takeIf { it.isNotBlank() }?.let { appendLine("    notes: $it") }
            }
        }

        section("Recordings")
        safe("recordings") {
            val files = Recordings.list(c)
            if (files.isEmpty()) appendLine("(none)")
            files.forEach { appendLine("${it.name} · ${it.length() / 1_000_000} MB") }
        }

        section("Previous process exits")
        safe("exit info") {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                val exits = c.getSystemService(ActivityManager::class.java).getHistoricalProcessExitReasons(c.packageName, 0, 8)
                if (exits.isEmpty()) appendLine("(none recorded)")
                for (e in exits) {
                    appendLine("${Instant.ofEpochMilli(e.timestamp)} · ${GhostNetApp.exitReasonName(e.reason)}" +
                        " · status ${e.status} · importance ${e.importance}${e.description?.let { " · $it" } ?: ""}")
                    if (e.reason == android.app.ApplicationExitInfo.REASON_ANR) {
                        e.traceInputStream?.bufferedReader()?.use { r ->
                            r.lineSequence().take(150).forEach { appendLine("    $it") }
                        }
                    }
                }
            } else {
                appendLine("(needs Android 11+)")
            }
        }

        section("Diagnostics log")
        for (f in listOfNotNull(DiagLog.previous, DiagLog.current)) {
            if (f.exists()) {
                appendLine("--- ${f.name} (${f.length()} bytes) ---")
                append(tail(f, 3_000_000))
            }
        }

        section("Logcat (this app only)")
        safe("logcat") {
            val p = ProcessBuilder("logcat", "-d", "-v", "threadtime", "-t", "4000").redirectErrorStream(true).start()
            val out = p.inputStream.bufferedReader().readText()
            p.waitFor(5, TimeUnit.SECONDS)
            append(out.takeLast(1_500_000))
        }
    }

    private fun tail(f: File, maxChars: Int): String {
        val text = f.readText()
        return if (text.length <= maxChars) text else "(earlier lines trimmed)\n" + text.takeLast(maxChars)
    }
}
