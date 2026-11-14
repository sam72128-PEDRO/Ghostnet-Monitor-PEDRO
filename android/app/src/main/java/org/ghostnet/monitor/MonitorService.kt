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
import android.util.Log
import java.util.concurrent.Executors
import java.util.concurrent.ScheduledFuture
import java.util.concurrent.TimeUnit
import kotlin.math.abs

/**
 * Owns one receive session end to end: the USB connection, the native
 * receiver, the wake lock, the database session row and the foreground
 * notification. Monitoring continues with the screen off.
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
    private var wakeLock: PowerManager.WakeLock? = null
    private var statsTask: ScheduledFuture<*>? = null
    private var lastNotifiedMessages = -1L

    private val detachReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            if (intent.action != UsbManager.ACTION_USB_DEVICE_DETACHED) return
            val gone = intent.usbDevice() ?: return
            if (gone.deviceName == device?.deviceName) {
                control.execute { stopSession("Interrupted", "device_removed", "USB receiver was disconnected") }
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
            if (recovered > 0) Log.w(TAG, "marked $recovered unfinished session(s) Interrupted")
        }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_STOP -> control.execute { stopSession("Completed", "stop", null) }
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
        MonitorState.setSettings(s)
        MonitorState.setPhase(MonitorState.Phase.CONNECTING)

        val usb = getSystemService(UsbManager::class.java)
        val dev = usb.deviceList.values.firstOrNull { it.deviceName == deviceName } ?: RtlSdrDevices.find(usb)
        if (dev == null) return fail("No RTL-SDR receiver connected")
        if (!usb.hasPermission(dev)) return fail("USB permission for the receiver was not granted")

        val conn = usb.openDevice(dev) ?: return fail("Android refused to open the USB receiver")
        val rx = NativeReceiver()
        rx.openFd(conn.fileDescriptor)?.let { err ->
            rx.close()
            conn.close()
            return fail(err)
        }
        MonitorState.setPhase(MonitorState.Phase.READY)

        val start = System.currentTimeMillis()
        val id = db.startSession(s, "${dev.manufacturerName ?: ""} ${dev.productName ?: ""} ${dev.serialNumberOrEmpty()}".trim(), start)
        sessionId = id
        settings = s
        receiver = rx
        connection = conn
        device = dev

        rx.start(s, Listener(id))?.let { err ->
            stopSession("Failed", "start_failed", err)
            MonitorState.setPhase(MonitorState.Phase.ERROR, err)
            return
        }

        val described = rx.describe()
        MonitorState.beginSession(s, id, start, described)
        MonitorState.setPhase(MonitorState.Phase.RECEIVING)
        acquireWakeLock()
        lastNotifiedMessages = -1
        statsTask = control.scheduleWithFixedDelay({ pollStats() }, 1, 1, TimeUnit.SECONDS)
        Log.i(TAG, "session $id receiving: $described")
    }

    private fun fail(message: String) {
        Log.w(TAG, message)
        MonitorState.setPhase(MonitorState.Phase.ERROR, message)
        stopForegroundCompat()
        stopSelf()
    }

    /** state: Completed / Interrupted / Failed (spec section 8.1). */
    private fun stopSession(state: String, reason: String, detail: String?) {
        val rx = receiver ?: run {
            if (MonitorState.phase != MonitorState.Phase.ERROR) MonitorState.setPhase(MonitorState.Phase.IDLE)
            stopForegroundCompat()
            stopSelf()
            return
        }
        MonitorState.setPhase(MonitorState.Phase.STOPPING)
        statsTask?.cancel(false)
        statsTask = null

        val finalStats = runCatching { rx.stats() }.getOrNull()
        rx.stop() // delivers any half-received messages before returning
        rx.close()
        connection?.close()
        receiver = null
        connection = null
        device = null
        releaseWakeLock()

        val id = sessionId
        sessionId = null
        val end = System.currentTimeMillis()
        val clockNote = finalStats?.let { clockNote(it) }
        val notes = listOfNotNull(
            detail,
            finalStats?.let { "decode passes ${it.decodePasses}, IQ ${it.iqBytes} bytes, dropped ${it.droppedBytes} bytes" },
        ).joinToString("; ")
        if (id != null) dbWriter.execute { db.endSession(id, state, reason, end, clockNote, notes) }

        val phase = if (state == "Completed") MonitorState.Phase.IDLE else MonitorState.Phase.ERROR
        MonitorState.setPhase(phase, if (state == "Completed") null else (detail ?: reason))
        stopForegroundCompat()
        stopSelf()
    }

    private fun pollStats() {
        val rx = receiver ?: return
        val s = rx.stats()
        MonitorState.updateStats(s)
        if (s.messages != lastNotifiedMessages) {
            lastNotifiedMessages = s.messages
            settings?.let { notify("${it.preset.label} · ${s.frames} frames, ${s.messages} messages") }
        }
    }

    private inner class Listener(private val sessionId: String) : NativeListener {
        override fun onFrame(
            utcMs: Long, submode: Int, offsetHz: Double, snrDb: Int, dtS: Float, flags: Int,
            kind: String, raw: String, text: String, from: String, to: String,
        ) {
            val f = FrameRecord(utcMs, System.currentTimeMillis(), submode, offsetHz, snrDb, dtS, flags, kind, raw, text, from, to)
            dbWriter.execute { runCatching { db.addFrame(sessionId, f) }.onFailure { Log.e(TAG, "frame write", it) } }
            MonitorState.addFrame(f)
        }

        override fun onMessage(
            firstUtcMs: Long, lastUtcMs: Long, submode: Int, offsetHz: Double, snrMinDb: Int, snrMaxDb: Int,
            frameCount: Int, complete: Boolean, missingStart: Boolean, hasGap: Boolean, timedOut: Boolean,
            text: String, from: String, to: String, grid: String, rawFrames: String,
        ) {
            val m = MessageRecord(firstUtcMs, lastUtcMs, submode, offsetHz, snrMinDb, snrMaxDb, frameCount, complete,
                missingStart, hasGap, timedOut, text, from, to, grid, rawFrames)
            dbWriter.execute { runCatching { db.addMessage(sessionId, m) }.onFailure { Log.e(TAG, "message write", it) } }
            MonitorState.addMessage(m)
        }

        override fun onSourceError(message: String) {
            // Called on the native USB thread: hand off, never stop from here.
            control.execute { stopSession("Interrupted", "source_error", message) }
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
        Log.e(TAG, "cannot enter foreground", e)
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
        const val ACTION_START = "org.ghostnet.monitor.START"
        const val ACTION_STOP = "org.ghostnet.monitor.STOP"
        const val EXTRA_DEVICE_NAME = "device_name"
        private const val CHANNEL = "ghostnet-monitor"
        private const val NOTIFICATION_ID = 7
        private const val TAG = "GhostNetMonitor"

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
