package org.ghostnet.monitor

import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbManager

/** Recognises RTL2832U receivers among attached USB devices. */
object RtlSdrDevices {
    // RTL2832U-based dongles, including the RTL-SDR Blog V3 (0bda:2838).
    // Keep in sync with res/xml/rtlsdr_device_filter.xml.
    private val KNOWN = setOf(
        0x0bda to 0x2832,
        0x0bda to 0x2838,
        0x0413 to 0x6680,
        0x0413 to 0x6f0f,
        0x0458 to 0x707f,
        0x0ccd to 0x00a9,
        0x0ccd to 0x00b3,
        0x0ccd to 0x00d3,
        0x0ccd to 0x00e0,
        0x185b to 0x0620,
        0x185b to 0x0650,
        0x1b80 to 0xd393,
        0x1b80 to 0xd394,
        0x1b80 to 0xd395,
        0x1b80 to 0xd39d,
        0x1d19 to 0x1101,
        0x1d19 to 0x1102,
        0x1d19 to 0x1103,
        0x1f4d to 0xb803,
        0x1f4d to 0xc803,
    )

    fun isRtlSdr(d: UsbDevice): Boolean = (d.vendorId to d.productId) in KNOWN

    fun find(usb: UsbManager): UsbDevice? = usb.deviceList.values.firstOrNull(::isRtlSdr)

    fun describe(d: UsbDevice): String {
        val name = listOfNotNull(d.manufacturerName, d.productName).joinToString(" ").ifBlank { "USB device" }
        return "%s (%04x:%04x)".format(name, d.vendorId, d.productId)
    }
}
