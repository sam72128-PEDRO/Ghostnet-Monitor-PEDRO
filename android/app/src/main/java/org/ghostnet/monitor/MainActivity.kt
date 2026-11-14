package org.ghostnet.monitor

import android.Manifest
import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.hardware.usb.UsbManager
import android.os.Build
import android.os.Bundle
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.runtime.mutableIntStateOf
import org.ghostnet.monitor.ui.AppController
import org.ghostnet.monitor.ui.GhostNetMonitorApp
import org.ghostnet.monitor.ui.GhostNetTheme

/**
 * Hosts the four-tab Compose UI and owns the platform interactions it needs:
 * USB permission, starting/stopping the monitoring service, toasts. Receive
 * only: there is no transmit, reply or keying action anywhere in the app.
 */
class MainActivity : ComponentActivity(), AppController {

    override lateinit var usb: UsbManager
    /** Bumped whenever MonitorState changes; the UI reads it to recompose. */
    override val tick = mutableIntStateOf(0)
    private var startAfterPermission = false
    private val refresh: () -> Unit = { tick.intValue++ }

    private val permissionReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            if (intent.action != ACTION_USB_PERMISSION) return
            val granted = intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)
            val dev = intent.usbDevice()
            DiagLog.log(if (granted) DiagLog.Level.INFO else DiagLog.Level.WARN, TAG,
                "USB permission ${if (granted) "granted" else "DENIED"} for ${dev?.let { RtlSdrDevices.describe(it) } ?: "?"}")
            if (granted && startAfterPermission) startMonitoring()
            else if (!granted) toast("USB permission denied. GhostNet Monitor cannot read the receiver without it.")
            startAfterPermission = false
            tick.intValue++
        }
    }

    override fun onCreate(state: Bundle?) {
        super.onCreate(state)
        usb = getSystemService(UsbManager::class.java)
        registerReceiver(permissionReceiver, IntentFilter(ACTION_USB_PERMISSION), RECEIVER_NOT_EXPORTED)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU &&
            checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED
        ) {
            requestPermissions(arrayOf(Manifest.permission.POST_NOTIFICATIONS), 1)
        }
        logUsbIntent(intent)
        setContent { GhostNetTheme { GhostNetMonitorApp(this) } }
    }

    override fun onStart() {
        super.onStart()
        MonitorState.observe(refresh)
    }

    override fun onStop() {
        MonitorState.unobserve(refresh)
        super.onStop()
    }

    override fun onResume() {
        super.onResume()
        tick.intValue++ // refresh countdowns and device state when returning
        // A session waiting for its receiver to come back: if it's back but
        // Android wants USB permission again, ask now; the service resumes
        // by itself once it's granted.
        if (MonitorState.phase == MonitorState.Phase.CONNECTING && MonitorState.error != null) {
            RtlSdrDevices.find(usb)?.takeIf { !usb.hasPermission(it) }?.let { requestUsbPermission() }
        }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        logUsbIntent(intent)
        tick.intValue++
    }

    override fun onDestroy() {
        unregisterReceiver(permissionReceiver)
        super.onDestroy()
    }

    private fun logUsbIntent(intent: Intent?) {
        if (intent?.action == UsbManager.ACTION_USB_DEVICE_ATTACHED) {
            val d = intent.usbDevice()
            DiagLog.i(TAG, "USB attached (app opened by Android): ${d?.let { RtlSdrDevices.describe(it) } ?: "?"}, " +
                "permission ${d?.let { usb.hasPermission(it) }}")
        }
    }

    // ---- AppController ----------------------------------------------------

    override fun startStop() {
        when (MonitorState.phase) {
            MonitorState.Phase.RECEIVING, MonitorState.Phase.CONNECTING, MonitorState.Phase.READY ->
                startService(Intent(this, MonitorService::class.java).setAction(MonitorService.ACTION_STOP))
            MonitorState.Phase.STOPPING -> Unit
            else -> {
                val dev = RtlSdrDevices.find(usb)
                when {
                    dev == null -> {
                        DiagLog.w(TAG, "start pressed but no RTL-SDR attached; USB devices: " +
                            usb.deviceList.values.joinToString { RtlSdrDevices.describe(it) }.ifEmpty { "none" })
                        toast("No RTL-SDR found. Connect it with a USB-C OTG cable or powered hub.")
                    }
                    !usb.hasPermission(dev) -> {
                        startAfterPermission = true
                        requestUsbPermission()
                    }
                    else -> startMonitoring()
                }
            }
        }
    }

    private fun startMonitoring() {
        val dev = RtlSdrDevices.find(usb) ?: return toast("Receiver disappeared")
        DiagLog.i(TAG, "starting monitoring service for ${RtlSdrDevices.describe(dev)}")
        startForegroundService(
            Intent(this, MonitorService::class.java)
                .setAction(MonitorService.ACTION_START)
                .putExtra(MonitorService.EXTRA_DEVICE_NAME, dev.deviceName),
        )
    }

    override fun requestUsbPermission() {
        val dev = RtlSdrDevices.find(usb) ?: return toast("No RTL-SDR found")
        if (usb.hasPermission(dev)) return toast("Permission already granted for ${RtlSdrDevices.describe(dev)}")
        DiagLog.i(TAG, "requesting USB permission for ${RtlSdrDevices.describe(dev)}")
        val pi = PendingIntent.getBroadcast(
            this, 0, Intent(ACTION_USB_PERMISSION).setPackage(packageName),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_MUTABLE,
        )
        usb.requestPermission(dev, pi)
    }

    override fun toast(msg: String) = Toast.makeText(this, msg, Toast.LENGTH_LONG).show()

    companion object {
        private const val ACTION_USB_PERMISSION = "org.ghostnet.monitor.USB_PERMISSION"
        private const val TAG = "ui"
    }
}
