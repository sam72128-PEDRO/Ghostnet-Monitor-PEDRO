package org.ghostnet.monitor

import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioTrack

/**
 * Live playback of demodulated voice (12 kHz mono). Audio goes straight to
 * the speaker or headphones and nowhere else: nothing is buffered beyond
 * the playback queue, recorded, logged or transcribed (spec section 2.3).
 *
 * Writes are non-blocking because they come from the native DSP thread;
 * if playback falls behind, audio is dropped rather than stalling the radio.
 */
class AudioPlayer(volume: Float) {
    private val track: AudioTrack
    private var started = false
    private var queued = 0

    init {
        val minBytes = AudioTrack.getMinBufferSize(RATE, AudioFormat.CHANNEL_OUT_MONO, AudioFormat.ENCODING_PCM_16BIT)
        track = AudioTrack.Builder()
            .setAudioAttributes(
                AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_MEDIA)
                    .setContentType(AudioAttributes.CONTENT_TYPE_SPEECH)
                    .build(),
            )
            .setAudioFormat(
                AudioFormat.Builder()
                    .setSampleRate(RATE)
                    .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                    .setChannelMask(AudioFormat.CHANNEL_OUT_MONO)
                    .build(),
            )
            .setBufferSizeInBytes(maxOf(minBytes, RATE * 2)) // ~1 s of slack
            .setTransferMode(AudioTrack.MODE_STREAM)
            .build()
        setVolume(volume)
    }

    fun setVolume(v: Float) {
        track.setVolume(v.coerceIn(0f, 1f))
    }

    fun write(pcm: ShortArray) {
        track.write(pcm, 0, pcm.size, AudioTrack.WRITE_NON_BLOCKING)
        if (!started) {
            queued += pcm.size
            // Prefill ~250 ms before starting so USB jitter doesn't cause gaps.
            if (queued >= RATE / 4) {
                track.play()
                started = true
            }
        }
    }

    fun release() {
        runCatching { track.pause(); track.flush() }
        track.release()
    }

    companion object {
        const val RATE = 12_000
    }
}
