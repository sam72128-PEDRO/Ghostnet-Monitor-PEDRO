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

    /** Waterfall row: dB per bin from 0 Hz above the dial, [binHz] wide each. About 12 rows/s. */
    fun onSpectrum(bins: FloatArray, binHz: Float)

    /** Voice modes: 12 kHz mono PCM for live playback only. Never stored. */
    fun onAudio(pcm: ShortArray)

    /** RTTY: one decoded character ("~" = framing error) as it arrives. */
    fun onRttyChar(text: String, framingError: Boolean)

    /** RTTY: a completed line of text. */
    fun onRttyLine(startMs: Long, endMs: Long, text: String, characters: Int, framingErrors: Int, centerHz: Double)

    /** ALE: one decoded 24-bit word. [chars] is the three characters as received. */
    fun onAleWord(timeMs: Long, channelHz: Int, preamble: String, chars: String, raw: Int, errors: Int, badVotes: Int)

    /** ALE: one station's transmission (sounding, call), when it ends. [to] is comma-separated. */
    fun onAleTransmission(
        startMs: Long, endMs: Long, channelHz: Int, kind: String, summary: String, from: String, to: String,
        amd: String, words: Int, maxErrors: Int,
    )
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
    fun start(settings: ReceiveSettings, recordPath: String?, recordMaxBytes: Long, listener: NativeListener): String? =
        nativeStart(
            handle, settings.preset.dialHz, settings.sampleRateHz, settings.captureOffsetHz, settings.preset.usb,
            settings.invertSpectrum, settings.ppm, settings.rtlAgc, settings.directSampling,
            settings.submodeMask, if (settings.mode.digital) recordPath else null, recordMaxBytes, settings.verboseLog,
            settings.mode.nativeId, 0.0, settings.rttyShiftHz.toDouble(), settings.rttyReverse, settings.fmSquelch,
            settings.preset.aleChannels.toIntArray(), settings.aleDwellMs, listener,
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
        val recording: Boolean,
        val recordedBytes: Long,
        val squelchOpen: Boolean = false,
        val fmNoise: Double = 1.0,
        val rttyLocked: Boolean = false,
        val rttyCenterHz: Double = 0.0,
        val rttyQuality: Double = 0.0,
        val rttyCharacters: Long = 0,
        val rttyFramingErrors: Long = 0,
        val aleChannelHz: Int = 0,
        val aleScanning: Boolean = false,
        val aleHolding: Boolean = false,
        val aleSync: Boolean = false,
        val aleQuality: Double = 0.0,
        val aleWords: Long = 0,
        val aleTransmissions: Long = 0,
        val retunes: Long = 0,
        val retuneFailures: Long = 0,
        val aleChannelIndex: Int = 0,
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
                recording = a[16] != 0.0,
                recordedBytes = a[17].toLong(),
                squelchOpen = a[18] != 0.0,
                fmNoise = a[19],
                rttyLocked = a[20] != 0.0,
                rttyCenterHz = a[21],
                rttyQuality = a[22],
                rttyCharacters = a[23].toLong(),
                rttyFramingErrors = a[24].toLong(),
                aleChannelHz = a[25].toInt(),
                aleScanning = a[26] != 0.0,
                aleHolding = a[27] != 0.0,
                aleSync = a[28] != 0.0,
                aleQuality = a[29],
                aleWords = a[30].toLong(),
                aleTransmissions = a[31].toLong(),
                retunes = a[32].toLong(),
                retuneFailures = a[33].toLong(),
                aleChannelIndex = a[34].toInt(),
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

        /**
         * Lines logged natively since the last call ("unixMs|source|text"),
         * including librtlsdr/libusb stderr output, which Android otherwise discards.
         */
        fun drainLog(): Array<String> = nativeDrainLog()

        @JvmStatic private external fun nativeCreate(): Long
        @JvmStatic private external fun nativeDestroy(handle: Long)
        @JvmStatic private external fun nativeOpenFd(handle: Long, fd: Int): String?
        @JvmStatic private external fun nativeStart(
            handle: Long, dialHz: Int, sampleRateHz: Int, captureOffsetHz: Int, usb: Boolean,
            invertSpectrum: Boolean, ppm: Int, rtlAgc: Boolean, directSampling: Int, submodeMask: Int,
            recordPath: String?, recordMaxBytes: Long, verboseLog: Boolean, mode: Int, rttyCenterHz: Double,
            rttyShiftHz: Double, rttyReverse: Boolean, fmSquelch: Boolean, aleChannels: IntArray, aleDwellMs: Int,
            listener: NativeListener,
        ): String?
        @JvmStatic private external fun nativeStop(handle: Long)
        @JvmStatic private external fun nativeDescribe(handle: Long): String
        @JvmStatic private external fun nativeStats(handle: Long): DoubleArray
        @JvmStatic private external fun nativeSelfTest(speed: Double): Array<String>
        @JvmStatic private external fun nativeDrainLog(): Array<String>
    }
}
