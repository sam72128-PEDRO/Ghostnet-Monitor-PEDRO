package org.ghostnet.monitor

import android.os.Handler
import android.os.Looper

/**
 * Process-wide view of the monitor for the UI. Written by MonitorService
 * (from any thread), observed on the main thread.
 */
object MonitorState {
    /** Spec section 2.4 session states. */
    enum class Phase { IDLE, CONNECTING, READY, RECEIVING, STOPPING, ERROR }

    data class FrameLine(val record: FrameRecord)
    data class MessageLine(val record: MessageRecord)

    @Volatile var phase: Phase = Phase.IDLE; private set
    @Volatile var settings: ReceiveSettings? = null; private set
    @Volatile var deviceText: String = ""; private set
    @Volatile var error: String? = null; private set
    @Volatile var stats: NativeReceiver.Stats? = null; private set
    @Volatile var sessionId: String? = null; private set  // null for voice (never logged)
    @Volatile var sessionStartMs: Long = 0; private set
    @Volatile var recordingName: String? = null; private set
    /** Playback volume for voice modes, applied live by the service. */
    @Volatile var volume: Float = 0.8f
    private val rttyLive = StringBuilder()

    /** The RTTY line currently being received (last ~300 characters). */
    fun rttyLive(): String = synchronized(lock) { rttyLive.toString() }

    fun appendRtty(text: String) {
        synchronized(lock) {
            if (text == "\r" || text == "\n") rttyLive.append(' ') else rttyLive.append(text)
            if (rttyLive.length > 300) rttyLive.delete(0, rttyLive.length - 300)
        }
        changed()
    }

    private const val MAX_LINES = 500
    private val lock = Any()
    private val frames = ArrayDeque<FrameLine>()
    private val messages = ArrayDeque<MessageLine>()

    private val main = Handler(Looper.getMainLooper())
    private val listeners = mutableSetOf<() -> Unit>()
    private var notifyPosted = false

    fun frames(): List<FrameLine> = synchronized(lock) { frames.toList() }
    fun messages(): List<MessageLine> = synchronized(lock) { messages.toList() }

    fun setPhase(p: Phase, error: String? = null) {
        phase = p
        this.error = error
        changed()
    }

    fun beginSession(settings: ReceiveSettings, sessionId: String?, startMs: Long, device: String, recording: String?) {
        synchronized(lock) {
            frames.clear()
            messages.clear()
            rttyLive.clear()
        }
        this.settings = settings
        this.sessionId = sessionId
        this.sessionStartMs = startMs
        this.deviceText = device
        this.recordingName = recording
        this.stats = null
        changed()
    }

    fun setDevice(text: String) {
        deviceText = text
        changed()
    }

    fun setSettings(s: ReceiveSettings) {
        settings = s
        changed()
    }

    fun updateStats(s: NativeReceiver.Stats) {
        stats = s
        changed()
    }

    fun addFrame(f: FrameRecord) {
        synchronized(lock) {
            frames.addFirst(FrameLine(f))
            while (frames.size > MAX_LINES) frames.removeLast()
        }
        changed()
    }

    fun addMessage(m: MessageRecord) {
        synchronized(lock) {
            messages.addFirst(MessageLine(m))
            while (messages.size > MAX_LINES) messages.removeLast()
        }
        changed()
    }

    fun observe(listener: () -> Unit) {
        main.post { listeners.add(listener); listener() }
    }

    fun unobserve(listener: () -> Unit) {
        main.post { listeners.remove(listener) }
    }

    /** Coalesces bursts of updates into one UI refresh per ~150 ms. */
    private fun changed() {
        synchronized(lock) {
            if (notifyPosted) return
            notifyPosted = true
        }
        main.postDelayed({
            synchronized(lock) { notifyPosted = false }
            listeners.toList().forEach { it() }
        }, 150)
    }
}
