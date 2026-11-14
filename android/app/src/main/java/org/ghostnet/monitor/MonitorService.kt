package org.ghostnet.monitor

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.ServiceInfo
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbDeviceConnection
import android.hardware.usb.UsbManager
import android.os.Build
import android.os.IBinder
import android.os.PowerManager
import android.provider.Settings
import java.io.File
import java.util.concurrent.Executors
import java.util.concurrent.ScheduledFuture
import java.util.concurrent.TimeUnit
import kotlin.math.abs

/**
 * Owns one receive session end to end: the USB connection, the native
 * receiver, the wake lock, the database session row, the optional IQ
 * recording and the foreground notification. Monitoring continues with the
 * screen off. Every step is written to the diagnostics log.
 *
 * Receive-only. Nothing here writes to a radio: the RTL-SDR has no
 * transmitter and the native layer exposes no transmit path.
 */
class MonitorService : Service() {

    private val control = Executors.newSingleThreadScheduledExecutor { Thread(it, "monitor-control") }
    private val dbWriter = Executors.newSingleThreadExecutor { Thread(it, "monitor-db") }
    private lateinit var db: SessionDatabase

    private var receiver: NativeReceiver? = null
    private var connection: UsbDeviceConnection? = null
    private var device: UsbDevice? = null
    private var sessionId: String? = null
    private var settings: ReceiveSettings? = null
    private var recording: File? = null
    private var player: AudioPlayer? = null
    private var wakeLock: PowerManager.WakeLock? = null
    private var statsTask: ScheduledFuture<*>? = null
    private var lastNotifiedMessages = -1L
    private var pollCount = 0L
    // Reconnect after the receiver drops out mid-session (USB power or cable).
    private var reconnectTask: ScheduledFuture<*>? = null
    private var disconnectedAtMs = 0L
    private var reconnects = 0
    private val gapNotes = mutableListOf<String>()
    private var permissionNotified = false

    private val detachReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            if (intent.action != UsbManager.ACTION_USB_DEVICE_DETACHED) return
            val gone = intent.usbDevice() ?: return
            DiagLog.w(TAG, "USB detached: ${RtlSdrDevices.describe(gone)} at ${gone.deviceName}")
            if (gone.deviceName == device?.deviceName) {
                control.execute { suspendForReconnect("USB receiver was disconnected") }
            }
        }
    }

    override fun onCreate() {
        super.onCreate()
        db = SessionDatabase(this)
        getSystemService(NotificationManager::class.java).createNotificationChannel(
            NotificationChannel(CHANNEL, "GhostNet monitoring", NotificationManager.IMPORTANCE_LOW),
        )
        registerReceiver(detachReceiver, IntentFilter(UsbManager.ACTION_USB_DEVICE_DETACHED), RECEIVER_NOT_EXPORTED)
        dbWriter.execute {
            val recovered = db.recoverInterruptedSessions()
            if (recovered > 0) DiagLog.w(TAG, "marked $recovered unfinished session(s) Interrupted (app ended mid-session)")
        }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_STOP -> {
                DiagLog.i(TAG, "stop requested")
                control.execute { stopSession("Completed", "stop", null) }
            }
            ACTION_START -> {
                // Must enter the foreground promptly after startForegroundService().
                if (!enterForeground("Connecting to receiver…")) return START_NOT_STICKY
                val deviceName = intent.getStringExtra(EXTRA_DEVICE_NAME)
                val s = ReceiveSettings.load(this)
                control.execute { startSession(deviceName, s) }
            }
        }
        return START_NOT_STICKY
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onDestroy() {
        control.submit { stopSession("Interrupted", "service_destroyed", "Monitoring service was stopped by the system") }
            .runCatching { get(10, TimeUnit.SECONDS) }
        control.shutdown()
        dbWriter.shutdown()
        dbWriter.awaitTermination(5, TimeUnit.SECONDS)
        unregisterReceiver(detachReceiver)
        db.close()
        super.onDestroy()
    }

    // ---- session lifecycle (all on the control thread) ---------------------

    private fun startSession(deviceName: String?, s: ReceiveSettings) {
        if (receiver != null) return
        DiagLog.i(TAG, "start: ${s.describe()}")
        if (Settings.Global.getInt(contentResolver, Settings.Global.AUTO_TIME, 1) == 0) {
            DiagLog.w(TAG, "automatic date & time is OFF; JS8 needs the clock within about 1 s of UTC")
        }
        MonitorState.setSettings(s)
        MonitorState.setPhase(MonitorState.Phase.CONNECTING)

        val usb = getSystemService(UsbManager::class.java)
        DiagLog.i(TAG, "USB devices: " + usb.deviceList.values.joinToString { RtlSdrDevices.describe(it) }.ifEmpty { "none" })
        val dev = usb.deviceList.values.firstOrNull { it.deviceName == deviceName } ?: RtlSdrDevices.find(usb)
        if (dev == null) return fail("No RTL-SDR receiver connected")
        if (!usb.hasPermission(dev)) return fail("USB permission for ${RtlSdrDevices.describe(dev)} was not granted")

        val conn = usb.openDevice(dev) ?: return fail("Android refused to open ${RtlSdrDevices.describe(dev)} (another app may be using it)")
        DiagLog.i(TAG, "opened ${RtlSdrDevices.describe(dev)} fd=${conn.fileDescriptor}")
        val rx = NativeReceiver()
        rx.openFd(conn.fileDescriptor)?.let { err ->
            rx.close()
            conn.close()
            return fail("Receiver did not initialise: $err")
        }
        MonitorState.setPhase(MonitorState.Phase.READY)
        reconnects = 0
        gapNotes.clear()

        val start = System.currentTimeMillis()
        // Digital modes get a session row; analog voice never creates one,
        // and is never recorded or logged (spec sections 2.3 and 8.5).
        val id = if (s.mode.digital) {
            db.startSession(s, "${dev.manufacturerName ?: ""} ${dev.productName ?: ""} ${dev.serialNumberOrEmpty()}".trim(), start)
        } else null
        sessionId = id
        settings = s
        receiver = rx
        connection = conn
        device = dev
        recording = if (s.recordIq && s.mode.digital) Recordings.newFile(this, s.preset.dialHz) else null
        player = if (!s.mode.digital) AudioPlayer(s.volume).also { MonitorState.volume = s.volume } else null
        Waterfall.clear()

        rx.start(s, recording?.path, s.recordMaxBytes, Listener(id))?.let { err ->
            DiagLog.e(TAG, "receiver start failed: $err")
            stopSession("Failed", "start_failed", err)
            MonitorState.setPhase(MonitorState.Phase.ERROR, err)
            return
        }

        val described = rx.describe()
        MonitorState.beginSession(s, id, start, described, recording?.name)
        MonitorState.setPhase(MonitorState.Phase.RECEIVING)
        acquireWakeLock()
        lastNotifiedMessages = -1
        pollCount = 0
        statsTask = control.scheduleWithFixedDelay({ pollStats() }, 1, 1, TimeUnit.SECONDS)
        DiagLog.i(TAG, (if (id != null) "session $id" else "voice listening (not logged)") +
            " receiving ${s.mode.label}: $described" + (recording?.let { ", recording to ${it.name}" } ?: ""))
    }

    /**
     * The receiver vanished mid-session (unplugged, USB power dropped, or the
     * stream died). Keep the session and the foreground service, and resume
     * as soon as the dongle is back; give up after [RECONNECT_WINDOW_MS].
     */
    private fun suspendForReconnect(detail: String) {
        val rx = receiver ?: return
        statsTask?.cancel(false)
        statsTask = null
        runCatching { rx.stop() }
        rx.close()
        connection?.close()
        receiver = null
        connection = null
        device = null
        recording?.let { gapNotes.add("IQ recording ${it.name} (${it.length()} bytes) ended at the disconnect") }
        recording = null
        disconnectedAtMs = System.currentTimeMillis()
        permissionNotified = false
        DiagLog.w(TAG, "receiver lost: $detail; session kept, waiting up to ${RECONNECT_WINDOW_MS / 60_000} min for it to come back")
        MonitorState.setPhase(MonitorState.Phase.CONNECTING,
            "Receiver disconnected at ${java.time.Instant.ofEpochMilli(disconnectedAtMs)}. Waiting for it to come back; the session is kept.")
        notify("Receiver disconnected · will resume automatically when it's back")
        reconnectTask?.cancel(false)
        reconnectTask = control.scheduleWithFixedDelay({ tryReconnect() }, 2, 2, TimeUnit.SECONDS)
    }

    private fun tryReconnect() {
        if (receiver != null) return
        val s = settings ?: return
        val waited = System.currentTimeMillis() - disconnectedAtMs
        if (waited > RECONNECT_WINDOW_MS) {
            stopSession("Interrupted", "device_removed",
                "Receiver disconnected and did not come back within ${RECONNECT_WINDOW_MS / 60_000} min")
            return
        }
        val usb = getSystemService(UsbManager::class.java)
        val dev = RtlSdrDevices.find(usb) ?: return
        if (!usb.hasPermission(dev)) {
            if (!permissionNotified) {
                permissionNotified = true
                DiagLog.w(TAG, "receiver is back but USB permission is needed again")
                notify("Receiver is back · open GhostNet Monitor to allow USB access")
            }
            return
        }
        val conn = usb.openDevice(dev) ?: return
        val rx = NativeReceiver()
        rx.openFd(conn.fileDescriptor)?.let { err ->
            DiagLog.w(TAG, "reconnect: receiver did not initialise yet: $err")
            rx.close()
            conn.close()
            return
        }
        recording = if (s.recordIq && s.mode.digital) Recordings.newFile(this, s.preset.dialHz) else null
        rx.start(s, recording?.path, s.recordMaxBytes, Listener(sessionId))?.let { err ->
            DiagLog.w(TAG, "reconnect: receiver start failed: $err")
            rx.close()
            conn.close()
            recording = null
            return
        }
        reconnectTask?.cancel(false)
        reconnectTask = null
        receiver = rx
        connection = conn
        device = dev
        reconnects++
        val gap = "receiver offline ${waited / 1000} s from ${java.time.Instant.ofEpochMilli(disconnectedAtMs)}"
        gapNotes.add(gap)
        val described = rx.describe()
        MonitorState.setDevice(described)
        MonitorState.setPhase(MonitorState.Phase.RECEIVING)
        statsTask = control.scheduleWithFixedDelay({ pollStats() }, 1, 1, TimeUnit.SECONDS)
        lastNotifiedMessages = -1
        DiagLog.i(TAG, "receiver reconnected ($gap); session ${sessionId ?: "(voice)"} resumed: $described" +
            (recording?.let { ", recording to ${it.name}" } ?: ""))
    }

    private fun fail(message: String) {
        DiagLog.e(TAG, message)
        MonitorState.setPhase(MonitorState.Phase.ERROR, message)
        stopForegroundCompat()
        stopSelf()
    }

    /** state: Completed / Interrupted / Failed (spec section 8.1). */
    private fun stopSession(state: String, reason: String, detail: String?) {
        reconnectTask?.cancel(false)
        reconnectTask = null
        val waiting = receiver == null && settings != null && disconnectedAtMs > 0
        val rx = receiver ?: if (waiting) null else run {
            if (MonitorState.phase != MonitorState.Phase.ERROR) MonitorState.setPhase(MonitorState.Phase.IDLE)
            stopForegroundCompat()
            stopSelf()
            return
        }
        MonitorState.setPhase(MonitorState.Phase.STOPPING)
        statsTask?.cancel(false)
        statsTask = null

        val finalStats = rx?.let { r -> runCatching { r.stats() }.getOrNull() }
        rx?.stop() // delivers any half-received messages before returning
        rx?.close()
        connection?.close()
        receiver = null
        connection = null
        device = null
        player?.release()
        player = null
        releaseWakeLock()

        val id = sessionId
        sessionId = null
        val end = System.currentTimeMillis()
        val clockNote = finalStats?.let { clockNote(it) }
        val notes = listOfNotNull(
            detail,
            if (reconnects > 0 || gapNotes.isNotEmpty()) "reconnected $reconnects time(s): ${gapNotes.joinToString("; ")}" else null,
            finalStats?.let { "decode passes ${it.decodePasses}, IQ ${it.iqBytes} bytes, dropped ${it.droppedBytes} bytes" },
            recording?.let { "IQ recording ${it.name} (${it.length()} bytes)" },
        ).joinToString("; ")
        if (id != null) dbWriter.execute { db.endSession(id, state, reason, end, clockNote, notes) }
        DiagLog.log(
            if (state == "Completed") DiagLog.Level.INFO else DiagLog.Level.WARN, TAG,
            "session $id ended: $state ($reason)${detail?.let { " · $it" } ?: ""} · $notes" + (clockNote?.let { " · $it" } ?: ""),
        )
        recording = null
        disconnectedAtMs = 0
        reconnects = 0
        gapNotes.clear()
        settings = null

        val phase = if (state == "Completed") MonitorState.Phase.IDLE else MonitorState.Phase.ERROR
        MonitorState.setPhase(phase, if (state == "Completed") null else (detail ?: reason))
        stopForegroundCompat()
        stopSelf()
    }

    /**
     * Spectrum orientation is a property of the dongle and the HF direct-
     * sampling path, not of the mode: once a JS8 decode confirms it, save it
     * so RTTY and ALE use the same setting and the automatic check stops.
     */
    private fun rememberOrientation(s: NativeReceiver.Stats) {
        val cur = settings ?: return
        if (!s.invertConfirmed || cur.mode != RxMode.JS8 || cur.preset.dialHz >= 24_000_000) return
        val saved = ReceiveSettings.load(this)
        if (saved.invertConfirmed && saved.invertSpectrum == s.invertActive) return
        ReceiveSettings.save(this, saved.copy(invertSpectrum = s.invertActive, invertConfirmed = true))
        DiagLog.i(TAG, "spectrum orientation confirmed by a JS8 decode: invert=${s.invertActive}" +
            if (s.invertFlips > 0) " (found by the automatic check)" else "")
    }

    private fun pollStats() {
        val rx = receiver ?: return
        val s = rx.stats()
        MonitorState.updateStats(s)
        rememberOrientation(s)
        if (s.messages != lastNotifiedMessages) {
            lastNotifiedMessages = s.messages
            settings?.let {
                notify(
                    when (it.mode) {
                        RxMode.JS8 -> "${it.preset.label} · ${s.frames} frames, ${s.messages} messages"
                        RxMode.RTTY -> "${it.preset.label} · ${s.messages} lines"
                        RxMode.ALE -> "${it.preset.label} · ${s.aleTransmissions} calls/soundings, ${s.aleWords} words"
                        else -> "${it.preset.label} · listening (audio is not recorded)"
                    },
                )
            }
        }
        // A periodic snapshot in the diagnostics log shows how things evolved
        // over a session, not just how it ended.
        if (pollCount++ % 30 == 0L) {
            DiagLog.i(
                TAG,
                "stats: IQ %.1f dBFS clip %.2f%% · channel %.1f dBFS AGC %+.0f dB · passes %d frames %d msgs %d · dt %+.2f s (%d) · dropped %d B%s".format(
                    s.iqLevelDbfs, s.clipPercent, s.channelLevelDbfs, s.agcGainDb, s.decodePasses, s.frames, s.messages,
                    s.medianDtS, s.dtSamples, s.droppedBytes, if (s.recording) " · recording ${s.recordedBytes / 1_000_000} MB" else "",
                ),
            )
        }
    }

    private inner class Listener(private val sessionId: String?) : NativeListener {
        private var appliedVolume = -1f
        override fun onFrame(
            utcMs: Long, submode: Int, offsetHz: Double, snrDb: Int, dtS: Float, flags: Int,
            kind: String, raw: String, text: String, from: String, to: String,
        ) {
            val f = FrameRecord(utcMs, System.currentTimeMillis(), submode, offsetHz, snrDb, dtS, flags, kind, raw, text, from, to)
            sessionId?.let { id -> dbWriter.execute { runCatching { db.addFrame(id, f) }.onFailure { DiagLog.e(TAG, "frame write failed", it) } } }
            MonitorState.addFrame(f)
            DiagLog.d(TAG, "frame %s %s %+.0f Hz %d dB dt%+.1f flags=%d %s [%s]".format(
                java.time.Instant.ofEpochMilli(utcMs), submodeName(submode), offsetHz, snrDb, dtS, flags, kind, raw))
        }

        override fun onMessage(
            firstUtcMs: Long, lastUtcMs: Long, submode: Int, offsetHz: Double, snrMinDb: Int, snrMaxDb: Int,
            frameCount: Int, complete: Boolean, missingStart: Boolean, hasGap: Boolean, timedOut: Boolean,
            text: String, from: String, to: String, grid: String, rawFrames: String,
        ) {
            val m = MessageRecord(firstUtcMs, lastUtcMs, submode, offsetHz, snrMinDb, snrMaxDb, frameCount, complete,
                missingStart, hasGap, timedOut, text, from, to, grid, rawFrames)
            sessionId?.let { id -> dbWriter.execute { runCatching { db.addMessage(id, m) }.onFailure { DiagLog.e(TAG, "message write failed", it) } } }
            MonitorState.addMessage(m)
        }

        override fun onSourceError(message: String) {
            DiagLog.e(TAG, "source error: $message")
            // Called on the native USB thread: hand off, never stop from here.
            control.execute { suspendForReconnect(message) }
        }

        override fun onSpectrum(bins: FloatArray, binHz: Float) = Waterfall.addRow(bins)

        override fun onAudio(pcm: ShortArray) {
            val p = player ?: return
            val v = MonitorState.volume
            if (v != appliedVolume) {
                p.setVolume(v)
                appliedVolume = v
            }
            p.write(pcm)
        }

        override fun onRttyChar(text: String, framingError: Boolean) = MonitorState.appendRtty(text)

        override fun onRttyLine(startMs: Long, endMs: Long, text: String, characters: Int, framingErrors: Int, centerHz: Double) {
            // Stored as a message: complete = no framing errors. '~' marks each
            // character that had one; nothing is guessed or repaired.
            val m = MessageRecord(startMs, endMs, 0, centerHz, 0, 0, characters, framingErrors == 0, false, false, false,
                text, "", "", "", "")
            sessionId?.let { id -> dbWriter.execute { runCatching { db.addMessage(id, m) }.onFailure { DiagLog.e(TAG, "RTTY line write failed", it) } } }
            MonitorState.addMessage(m)
            DiagLog.d(TAG, "RTTY line: $characters chars, $framingErrors framing errors, centre %.0f Hz".format(centerHz))
        }

        // ALE is stored in the same tables as JS8: each word is a frame and
        // each transmission a message. offset_hz holds the channel's dial
        // frequency (scanning changes it), flags the Golay correction count.
        override fun onAleWord(timeMs: Long, channelHz: Int, preamble: String, chars: String, raw: Int, errors: Int, badVotes: Int) {
            val f = FrameRecord(timeMs, System.currentTimeMillis(), 0, channelHz.toDouble(), 0, 0f, errors, preamble,
                "%06X v%d/48".format(raw and 0xFFFFFF, 48 - badVotes), "$preamble $chars", "", "")
            sessionId?.let { id -> dbWriter.execute { runCatching { db.addFrame(id, f) }.onFailure { DiagLog.e(TAG, "ALE word write failed", it) } } }
            MonitorState.addFrame(f)
            DiagLog.d(TAG, "ALE %.4f MHz %s %s (errors %d, votes %d/48)".format(channelHz / 1e6, preamble, chars, errors, 48 - badVotes))
        }

        override fun onAleTransmission(
            startMs: Long, endMs: Long, channelHz: Int, kind: String, summary: String, from: String, to: String,
            amd: String, words: Int, maxErrors: Int,
        ) {
            val complete = kind == "call" || kind == "sounding"
            val m = MessageRecord(startMs, endMs, 0, channelHz.toDouble(), 0, 0, words, complete,
                !complete, false, false, summary, from, to, "", kind)
            sessionId?.let { id -> dbWriter.execute { runCatching { db.addMessage(id, m) }.onFailure { DiagLog.e(TAG, "ALE write failed", it) } } }
            MonitorState.addMessage(m)
            DiagLog.i(TAG, "ALE %.4f MHz %s: %s (%d words, max %d corrections)".format(channelHz / 1e6, kind, summary, words, maxErrors))
        }
    }

    // ---- foreground / notification / wake lock ------------------------------

    private fun enterForeground(text: String): Boolean = try {
        val n = buildNotification(text)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            startForeground(NOTIFICATION_ID, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE)
        } else {
            startForeground(NOTIFICATION_ID, n)
        }
        true
    } catch (e: RuntimeException) {
        DiagLog.e(TAG, "Android refused to start the foreground service", e)
        MonitorState.setPhase(MonitorState.Phase.ERROR, "Android refused to start background monitoring: ${e.message}")
        stopSelf()
        false
    }

    private fun notify(text: String) {
        getSystemService(NotificationManager::class.java).notify(NOTIFICATION_ID, buildNotification(text))
    }

    private fun buildNotification(text: String): Notification {
        val stop = PendingIntent.getService(
            this, 1, Intent(this, MonitorService::class.java).setAction(ACTION_STOP),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
        )
        val open = PendingIntent.getActivity(
            this, 2, Intent(this, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
        )
        return Notification.Builder(this, CHANNEL)
            .setSmallIcon(android.R.drawable.stat_sys_download)
            .setContentTitle("GhostNet monitoring (receive only)")
            .setContentText(text)
            .setContentIntent(open)
            .setOngoing(true)
            .addAction(Notification.Action.Builder(null, "STOP", stop).build())
            .build()
    }

    private fun stopForegroundCompat() {
        stopForeground(STOP_FOREGROUND_REMOVE)
    }

    private fun acquireWakeLock() {
        // Keeps the CPU running so USB capture and decoding continue with the
        // screen off. Released as soon as the session ends.
        wakeLock = getSystemService(PowerManager::class.java)
            .newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "GhostNetMonitor:receive")
            .apply { setReferenceCounted(false); acquire() }
    }

    private fun releaseWakeLock() {
        wakeLock?.takeIf { it.isHeld }?.release()
        wakeLock = null
    }

    companion object {
        /** How long a session waits for a dropped receiver to come back. */
        const val RECONNECT_WINDOW_MS = 10 * 60_000L
        const val ACTION_START = "org.ghostnet.monitor.START"
        const val ACTION_STOP = "org.ghostnet.monitor.STOP"
        const val EXTRA_DEVICE_NAME = "device_name"
        private const val CHANNEL = "ghostnet-monitor"
        private const val NOTIFICATION_ID = 7
        private const val TAG = "service"

        fun clockNote(s: NativeReceiver.Stats): String? = when {
            s.dtSamples < 3 -> null
            abs(s.medianDtS) > 1.0 -> "Median JS8 DT %+.2f s over %d decodes: device clock may be off".format(s.medianDtS, s.dtSamples)
            else -> "Median JS8 DT %+.2f s over %d decodes".format(s.medianDtS, s.dtSamples)
        }
    }
}

@Suppress("DEPRECATION")
fun Intent.usbDevice(): UsbDevice? =
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) getParcelableExtra(UsbManager.EXTRA_DEVICE, UsbDevice::class.java)
    else getParcelableExtra(UsbManager.EXTRA_DEVICE)

fun UsbDevice.serialNumberOrEmpty(): String = try {
    serialNumber ?: ""
} catch (e: SecurityException) {
    ""
}
