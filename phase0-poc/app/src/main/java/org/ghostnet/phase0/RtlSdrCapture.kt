package org.ghostnet.phase0

import android.hardware.usb.UsbConstants
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbDeviceConnection
import android.hardware.usb.UsbEndpoint
import android.hardware.usb.UsbInterface
import android.hardware.usb.UsbManager

/** Direct Android USB transport boundary for RTL2832U-compatible receivers. */
class RtlSdrCapture(private val usbManager: UsbManager) : AutoCloseable {
    private var connection: UsbDeviceConnection? = null
    private var input: UsbEndpoint? = null
    private var claimed: UsbInterface? = null

    fun open(device: UsbDevice): Boolean {
        close()
        if (!usbManager.hasPermission(device)) return false
        val usbInterface = (0 until device.interfaceCount)
            .map(device::getInterface)
            .firstOrNull { it.endpointCount > 0 } ?: return false
        val endpoint = (0 until usbInterface.endpointCount)
            .map(usbInterface::getEndpoint)
            .firstOrNull { it.direction == UsbConstants.USB_DIR_IN && it.type == UsbConstants.USB_ENDPOINT_XFER_BULK }
            ?: return false
        val opened = usbManager.openDevice(device) ?: return false
        if (!opened.claimInterface(usbInterface, true)) {
            opened.close()
            return false
        }
        connection = opened
        claimed = usbInterface
        input = endpoint
        return true
    }

    fun read(buffer: ByteArray, timeoutMs: Int = 1000): Int {
        val connection = connection ?: return -1
        val endpoint = input ?: return -1
        return connection.bulkTransfer(endpoint, buffer, buffer.size, timeoutMs)
    }

    override fun close() {
        val current = connection
        val interfaceToRelease = claimed
        if (current != null && interfaceToRelease != null) current.releaseInterface(interfaceToRelease)
        current?.close()
        connection = null
        claimed = null
        input = null
    }
}
