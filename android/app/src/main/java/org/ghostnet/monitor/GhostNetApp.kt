package org.ghostnet.monitor

import android.app.ActivityManager
import android.app.Application
import android.app.ApplicationExitInfo
import android.os.Build
import java.time.Instant
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit

/**
 * Process start-up: diagnostics log, crash capture, the reason the previous
 * run ended, native log draining, and session retention.
 */
class GhostNetApp : Application() {

    override fun onCreate() {
        super.onCreate()
        DiagLog.init(this)
        DiagLog.i("app", "start · GhostNet Monitor ${BuildConfigInfo.VERSION} · ${Build.MANUFACTURER} ${Build.MODEL} · Android ${Build.VERSION.RELEASE} (API ${Build.VERSION.SDK_INT})")
        logPreviousExit()

        val previous = Thread.getDefaultUncaughtExceptionHandler()
        Thread.setDefaultUncaughtExceptionHandler { thread, error ->
            DiagLog.e("crash", "uncaught exception on thread '${thread.name}'", error)
            previous?.uncaughtException(thread, error)
        }

        // Loading the native library starts capturing librtlsdr/libusb stderr;
        // drain it into the diagnostics log once a second for the life of the process.
        Executors.newSingleThreadScheduledExecutor { Thread(it, "native-log").apply { isDaemon = true } }
            .scheduleWithFixedDelay({ drainNative() }, 0, 1, TimeUnit.SECONDS)

        Thread {
            val days = AppPrefs.retentionDays(this)
            if (days > 0) {
                val db = SessionDatabase(this)
                val removed = db.deleteSessionsEndedBefore(System.currentTimeMillis() - days * 86_400_000L)
                db.close()
                if (removed > 0) DiagLog.i("app", "retention: removed $removed session(s) older than $days days")
            }
        }.start()
    }

    private fun drainNative() {
        try {
            NativeReceiver.drainLog().forEach(DiagLog::native)
        } catch (t: Throwable) {
            DiagLog.e("app", "native log drain failed", t)
        }
    }

    private fun logPreviousExit() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) return
        val am = getSystemService(ActivityManager::class.java)
        val exit = am.getHistoricalProcessExitReasons(packageName, 0, 1).firstOrNull() ?: return
        val level = when (exit.reason) {
            ApplicationExitInfo.REASON_CRASH, ApplicationExitInfo.REASON_CRASH_NATIVE,
            ApplicationExitInfo.REASON_ANR, ApplicationExitInfo.REASON_LOW_MEMORY -> DiagLog.Level.ERROR
            else -> DiagLog.Level.INFO
        }
        DiagLog.log(level, "app", "previous run ended at ${Instant.ofEpochMilli(exit.timestamp)}: ${exitReasonName(exit.reason)}" +
            (exit.description?.let { " ($it)" } ?: ""))
    }

    companion object {
        fun exitReasonName(reason: Int): String = when (reason) {
            ApplicationExitInfo.REASON_ANR -> "app not responding (ANR)"
            ApplicationExitInfo.REASON_CRASH -> "Java/Kotlin crash"
            ApplicationExitInfo.REASON_CRASH_NATIVE -> "native crash"
            ApplicationExitInfo.REASON_EXIT_SELF -> "exited normally"
            ApplicationExitInfo.REASON_LOW_MEMORY -> "killed for low memory"
            ApplicationExitInfo.REASON_SIGNALED -> "killed by signal"
            ApplicationExitInfo.REASON_USER_REQUESTED -> "stopped by user"
            ApplicationExitInfo.REASON_USER_STOPPED -> "force-stopped by user"
            ApplicationExitInfo.REASON_EXCESSIVE_RESOURCE_USAGE -> "killed for excessive resource use"
            ApplicationExitInfo.REASON_DEPENDENCY_DIED -> "dependency died"
            ApplicationExitInfo.REASON_PERMISSION_CHANGE -> "permission changed"
            ApplicationExitInfo.REASON_INITIALIZATION_FAILURE -> "initialization failure"
            ApplicationExitInfo.REASON_OTHER -> "other (system)"
            else -> "unknown ($reason)"
        }
    }
}
