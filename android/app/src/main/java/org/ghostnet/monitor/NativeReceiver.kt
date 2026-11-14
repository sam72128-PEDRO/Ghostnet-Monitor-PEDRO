package org.ghostnet.monitor

/**
 * Callbacks from the native receive pipeline. Invoked on native worker
 * threads, never on the main thread; implementations must hand work off
 * rather than block, and must not call [NativeReceiver.stop] from inside a
 * callback.
 */
interface NativeListener {
    fun onFrame(
        utcMs: Long, submode: Int, offsetHz: Double, snrDb: Int, dtS: Float, flags: Int,
        kind: String, raw: String, text: String, from: String, to: String,
    )

    fun onMessage(
        firstUtcMs: Long, lastUtcMs: Long, submode: Int, offsetHz: Double, snrMinDb: Int, snrMaxDb: Int,
        frameCount: Int, complete: Boolean, missingStart: Boolean, hasGap: Boolean, timedOut: Boolean,
        text: String, from: String, to: String, grid: String, rawFrames: String,
    )

    fun onSourceError(message: String)
}

/**
 * Receive-only handle on the native RTL-SDR -> JS8 pipeline (rx-core). There
 * is deliberately no transmit, PTT, CAT or audio-output method here or in
 * the native library behind it.
 */
class NativeReceiver : AutoCloseable {
    private var handle: Long = nativeCreate()

    /** Opens the dongle from UsbDeviceConnection.getFileDescriptor(). Returns an error or null. */
    fun openFd(fd: Int): String? = nativeOpenFd(handle, fd)

    /** Returns an error message, or null once samples are flowing into the decoder. */
    fun start(settings: ReceiveSettings, listener: NativeListener): String? = nativeStart(
        handle, settings.preset.dialHz, settings.sampleRateHz, settings.captureOffsetHz, true,
        settings.invertSpectrum, settings.ppm, settings.rtlAgc, settings.directSampling,
        settings.submodeMask, listener,
    )

    /** Blocks until stopped; half-received messages arrive via onMessage before it returns. */
    fun stop() = nativeStop(handle)

    fun describe(): String = nativeDescribe(handle)

    fun stats(): Stats = Stats.fromArray(nativeStats(handle))

    override fun close() {
        if (handle != 0L) {
            nativeDestroy(handle)
            handle = 0L
        }
    }

    data class Stats(
        val running: Boolean,
        val iqBytes: Long,
        val droppedBytes: Long,
        val iqLevelDbfs: Double,
        val clipPercent: Double,
        val channelLevelDbfs: Double,
        val agcGainDb: Double,
        val decodePasses: Long,
        val frames: Long,
        val messages: Long,
        val medianDtS: Double,
        val dtSamples: Int,
        val lastIqUnixMs: Long,
        val lastFrameUnixMs: Long,
        val spectrumPowerDb: Double,
        val spectrumPeakDb: Double,
    ) {
        companion object {
            // Layout must match nativeStats() in ghostnet_jni.cpp.
            fun fromArray(a: DoubleArray) = Stats(
                running = a[0] != 0.0,
                iqBytes = a[1].toLong(),
                droppedBytes = a[2].toLong(),
                iqLevelDbfs = a[3],
                clipPercent = a[4],
                channelLevelDbfs = a[5],
                agcGainDb = a[6],
                decodePasses = a[7].toLong(),
                frames = a[8].toLong(),
                messages = a[9].toLong(),
                medianDtS = a[10],
                dtSamples = a[11].toInt(),
                lastIqUnixMs = a[12].toLong(),
                lastFrameUnixMs = a[13].toLong(),
                spectrumPowerDb = a[14],
                spectrumPeakDb = a[15],
            )
        }
    }

    data class SelfTestResult(
        val passed: Boolean,
        val expectedText: String,
        val decodedText: String,
        val framesDecoded: Int,
        val framesExpected: Int,
        val captureSeconds: Double,
        val wallSeconds: Double,
        val detail: String,
    ) {
        companion object {
            // Layout must match nativeSelfTest() in ghostnet_jni.cpp.
            fun fromArray(a: Array<String>) = SelfTestResult(
                passed = a[0] == "1",
                expectedText = a[1],
                decodedText = a[2],
                framesDecoded = a[3].toIntOrNull() ?: 0,
                framesExpected = a[4].toIntOrNull() ?: 0,
                captureSeconds = a[5].toDoubleOrNull() ?: 0.0,
                wallSeconds = a[6].toDoubleOrNull() ?: 0.0,
                detail = a[7],
            )
        }
    }

    companion object {
        init {
            System.loadLibrary("ghostnet_native")
        }

        /**
         * Runs the embedded synthetic JS8 transmission through the full native
         * receive chain. No radio needed. Blocks for roughly 8 s; call off the
         * main thread.
         */
        fun runSelfTest(speed: Double = 10.0): SelfTestResult = SelfTestResult.fromArray(nativeSelfTest(speed))

        @JvmStatic private external fun nativeCreate(): Long
        @JvmStatic private external fun nativeDestroy(handle: Long)
        @JvmStatic private external fun nativeOpenFd(handle: Long, fd: Int): String?
        @JvmStatic private external fun nativeStart(
            handle: Long, dialHz: Int, sampleRateHz: Int, captureOffsetHz: Int, usb: Boolean,
            invertSpectrum: Boolean, ppm: Int, rtlAgc: Boolean, directSampling: Int, submodeMask: Int,
            listener: NativeListener,
        ): String?
        @JvmStatic private external fun nativeStop(handle: Long)
        @JvmStatic private external fun nativeDescribe(handle: Long): String
        @JvmStatic private external fun nativeStats(handle: Long): DoubleArray
        @JvmStatic private external fun nativeSelfTest(speed: Double): Array<String>
    }
}
