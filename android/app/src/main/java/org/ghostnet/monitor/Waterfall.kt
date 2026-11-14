package org.ghostnet.monitor

import kotlin.math.max
import kotlin.math.min

/**
 * Scrolling waterfall of the audio band above the dial, fed by native
 * spectrum rows (~12/s, 10 Hz bins, 0-3 kHz). Each row is coloured relative
 * to a slowly tracked noise floor, so the display works at any input level.
 * Rows are written from the native callback thread and read by the UI.
 */
object Waterfall {
    const val WIDTH = 300
    const val HEIGHT = 240
    const val BIN_HZ = 10f
    const val SPAN_DB = 30f

    private val lock = Any()
    private val pixels = IntArray(WIDTH * HEIGHT) { BLACK }
    private var head = 0
    private var floorDb = Float.NaN

    @Volatile var version = 0L
        private set

    fun clear() = synchronized(lock) {
        pixels.fill(BLACK)
        head = 0
        floorDb = Float.NaN
        version++
    }

    fun addRow(bins: FloatArray) {
        if (bins.isEmpty()) return
        val sorted = bins.copyOf().also { it.sort() }
        val median = sorted[sorted.size / 2]
        synchronized(lock) {
            floorDb = if (floorDb.isNaN()) median else floorDb + 0.05f * (median - floorDb)
            val base = head * WIDTH
            for (x in 0 until WIDTH) {
                val v = if (x < bins.size) bins[x] else floorDb
                pixels[base + x] = color((v - floorDb - 3f) / SPAN_DB)
            }
            head = (head + 1) % HEIGHT
            version++
        }
    }

    /** Copies the image with the newest row at the top. */
    fun snapshot(into: IntArray) = synchronized(lock) {
        for (r in 0 until HEIGHT) {
            val src = ((head - 1 - r) % HEIGHT + HEIGHT) % HEIGHT
            System.arraycopy(pixels, src * WIDTH, into, r * WIDTH, WIDTH)
        }
    }

    private const val BLACK = 0xFF000000.toInt()

    // black -> blue -> cyan -> yellow -> red
    private fun color(t0: Float): Int {
        val t = min(1f, max(0f, t0))
        val (r, g, b) = when {
            t < 0.25f -> Triple(0f, 0f, t / 0.25f)
            t < 0.5f -> Triple(0f, (t - 0.25f) / 0.25f, 1f)
            t < 0.75f -> Triple((t - 0.5f) / 0.25f, 1f, 1f - (t - 0.5f) / 0.25f)
            else -> Triple(1f, 1f - (t - 0.75f) / 0.25f, 0f)
        }
        return (0xFF shl 24) or ((r * 255).toInt() shl 16) or ((g * 255).toInt() shl 8) or (b * 255).toInt()
    }
}
