package org.ghostnet.phase0

import android.app.Activity
import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbManager
import android.os.Bundle
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.LinearLayout
import android.widget.Spinner
import android.widget.TextView

class MainActivity : Activity() {
    private lateinit var usbManager: UsbManager
    private lateinit var status: TextView
    private lateinit var frequency: Spinner
    private var monitoring = false

    private val permissionReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            if (intent.action != ACTION_USB_PERMISSION) return
            val device = intent.getParcelableExtra(UsbManager.EXTRA_DEVICE, UsbDevice::class.java)
            val granted = intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)
            status.text = if (device == null) "USB permission response missing device"
            else "${describe(device)}\nPermission ${if (granted) "granted" else "denied"}"
        }
    }

    override fun onCreate(state: Bundle?) {
        super.onCreate(state)
        usbManager = getSystemService(USB_SERVICE) as UsbManager
        frequency = Spinner(this).apply {
            adapter = ArrayAdapter(this@MainActivity, android.R.layout.simple_spinner_dropdown_item,
                listOf("80m  3.575 MHz", "40m  7.107 MHz", "20m  14.107 MHz"))
        }
        status = TextView(this).apply { textSize = 16f; setPadding(24, 24, 24, 12) }
        val traffic = TextView(this).apply {
            text = "No GhostNet traffic yet\nGroups: @GHOSTNET, @GSTFLASH"
            setPadding(24, 24, 24, 24)
        }
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            addView(TextView(this@MainActivity).apply { text = "GhostNet Monitor"; textSize = 26f; setPadding(24, 24, 24, 8) })
            addView(TextView(this@MainActivity).apply { text = "Receive-only offline monitor"; setPadding(24, 0, 24, 12) })
            addView(frequency)
            addView(Button(this@MainActivity).apply { text = "START monitoring"; setOnClickListener { startMonitoring() } })
            addView(Button(this@MainActivity).apply { text = "STOP monitoring"; setOnClickListener { stopMonitoring() } })
            addView(Button(this@MainActivity).apply { text = "Scan USB / request permission"; setOnClickListener { scanUsb() } })
            addView(status)
            addView(traffic)
        }
        setContentView(root)
        registerReceiver(permissionReceiver, IntentFilter(ACTION_USB_PERMISSION), RECEIVER_NOT_EXPORTED)
        scanUsb()
    }

    private fun startMonitoring() {
        monitoring = true
        startForegroundService(Intent(this, GhostNetMonitorService::class.java))
        status.append("\nMonitoring requested at ${frequency.selectedItem}")
    }

    private fun stopMonitoring() {
        monitoring = false
        stopService(Intent(this, GhostNetMonitorService::class.java))
        status.append("\nMonitoring stopped")
    }

    private fun scanUsb() {
        val devices = usbManager.deviceList.values.toList()
        if (devices.isEmpty()) { status.text = "No USB devices detected"; return }
        val device = devices.first()
        status.text = devices.joinToString("\n\n") { describe(it) }
        if (!usbManager.hasPermission(device)) {
            val pending = PendingIntent.getBroadcast(this, device.deviceId,
                Intent(ACTION_USB_PERMISSION).setPackage(packageName),
                PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_MUTABLE)
            usbManager.requestPermission(device, pending)
        } else status.append("\n\nPermission already granted")
    }

    private fun describe(device: UsbDevice): String = "USB ${device.deviceName}\n" +
        "VID=0x${device.vendorId.toString(16).padStart(4, '0')} PID=0x${device.productId.toString(16).padStart(4, '0')}\n" +
        "interfaces=${device.interfaceCount}"

    override fun onDestroy() {
        if (monitoring) stopMonitoring()
        unregisterReceiver(permissionReceiver)
        super.onDestroy()
    }

    private companion object { const val ACTION_USB_PERMISSION = "org.ghostnet.phase0.USB_PERMISSION" }
}
