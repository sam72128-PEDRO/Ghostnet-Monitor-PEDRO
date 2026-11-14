package org.ghostnet.monitor

import android.content.Context

/** Receive modes available in this build. [nativeId] must match ghostnet_jni.cpp. */
enum class RxMode(val nativeId: Int, val label: String, val digital: Boolean, val protocol: String) {
    JS8(0, "JS8", true, "JS8"),
    RTTY(1, "RTTY", true, "RTTY"),
    VOICE_SSB(2, "Voice (SSB)", false, "VOICE_SSB"),
    VOICE_FM(3, "Voice (FM)", false, "VOICE_FM"),
    ALE(4, "ALE (2G)", true, "ALE_2G"),
}

/** A frequency the receiver can tune, with the mode used there. */
data class Preset(
    val id: String,
    val label: String,
    val dialHz: Int,
    val purpose: String,
    val mode: RxMode = RxMode.JS8,
    val usb: Boolean = true,
    /** ALE: dial frequencies to scan (USB). Empty = listen on [dialHz] only. */
    val aleChannels: List<Int> = emptyList(),
) {
    val isTest: Boolean get() = id.startsWith("test")
}

object Presets {
    /** GhostNet V1.5 2G ALE list (booklet: working draft). Scan all, or sit on one channel.
     * Declared before [ALL], which uses it during initialisation. */
    val ALE_CHANNELS = listOf(3_598_500, 7_104_500, 7_106_500, 7_108_500, 10_128_500,
        14_111_500, 14_113_500, 14_115_500, 18_108_500, 28_108_500)

    val ALL = listOf(
        // GhostNet V1.5 JS8 (USB)
        Preset("80m", "80 m · 3.575 MHz JS8", 3_575_000, "International bridges (NA-AUS, EUR-AUS)"),
        Preset("40m", "40 m · 7.107 MHz JS8", 7_107_000, "Thursday regional nets; AUS-PAC bridge"),
        Preset("20m", "20 m · 14.107 MHz JS8", 14_107_000, "Saturday intercontinental bridges"),
        // Not GhostNet frequencies: busy JS8 calling frequencies, for checking
        // reception between scheduled windows. Labelled so they can't be
        // mistaken for nets.
        Preset("test40", "TEST · JS8 calling 7.078 MHz", 7_078_000, "Receiver check only, not GhostNet"),
        Preset("test20", "TEST · JS8 calling 14.078 MHz", 14_078_000, "Receiver check only, not GhostNet"),
        Preset("test80", "TEST · JS8 calling 3.578 MHz", 3_578_000, "Receiver check only, not GhostNet"),

        // GhostNet V1.5 RTTY (45.45 baud, USB)
        Preset("rtty40", "40 m · 7.077 MHz RTTY", 7_077_000, "Regional recovery window, 45.45 baud", RxMode.RTTY),

        // GhostNet V1.5 analog voice: emergency LSB fallback and VHF/UHF FM simplex
        Preset("lsb40", "40 m · 7.190 MHz LSB voice", 7_190_000, "Emergency-only voice fallback", RxMode.VOICE_SSB, usb = false),
        Preset("fm2us", "VHF · 146.105 MHz FM", 146_105_000, "USA / Australia / Pacific", RxMode.VOICE_FM),
        Preset("fm70us", "UHF · 446.105 MHz FM", 446_105_000, "USA / Australia / Pacific", RxMode.VOICE_FM),
        Preset("fm2eu", "VHF · 144.605 MHz FM", 144_605_000, "Europe / Africa", RxMode.VOICE_FM),
        Preset("fm70eu", "UHF · 437.105 MHz FM", 437_105_000, "Europe / Africa", RxMode.VOICE_FM),
        // Not GhostNet: NOAA Weather Radio transmits continuously across the US,
        // which makes it the easiest way to confirm FM reception any time.
        Preset("testwx1", "TEST · NOAA Weather 162.550 MHz", 162_550_000, "Receiver check only, not GhostNet", RxMode.VOICE_FM),
        Preset("testwx2", "TEST · NOAA Weather 162.400 MHz", 162_400_000, "Receiver check only, not GhostNet", RxMode.VOICE_FM),
        Preset("testwx3", "TEST · NOAA Weather 162.475 MHz", 162_475_000, "Receiver check only, not GhostNet", RxMode.VOICE_FM),
    ) + alePresets()

    private fun alePresets(): List<Preset> =
        listOf(Preset("alescan", "ALE · scan all 10 GhostNet channels", ALE_CHANNELS[0],
            "Stops on activity, resumes after it ends", RxMode.ALE, aleChannels = ALE_CHANNELS)) +
            ALE_CHANNELS.mapIndexed { i, hz ->
                Preset("ale%02d".format(i + 1), "ALE · %.4f MHz (%02dGHOSTNET)".format(hz / 1e6, i + 1), hz,
                    "Single ALE channel, no scanning", RxMode.ALE)
            } + listOf(
                // Not GhostNet: amateur HFN/HFLink ALE channels, where stations
                // sound around the clock. For checking ALE reception any time.
                Preset("testale20", "TEST · amateur ALE 14.109 MHz", 14_109_000, "Receiver check only, not GhostNet", RxMode.ALE),
                Preset("testale40", "TEST · amateur ALE 7.102 MHz", 7_102_000, "Receiver check only, not GhostNet", RxMode.ALE),
            )

    /** Kept for callers that only deal with JS8. */
    val JS8: List<Preset> get() = ALL.filter { it.mode == RxMode.JS8 }

    fun forMode(mode: RxMode): List<Preset> = ALL.filter { it.mode == mode }

    fun byId(id: String?): Preset = ALL.firstOrNull { it.id == id } ?: ALL[1]

    /** The non-test preset for a published frequency, if the app can receive it. */
    fun forDial(hz: Long): Preset? = ALL.firstOrNull { !it.isTest && it.dialHz.toLong() == hz }
}

data class ReceiveSettings(
    val preset: Preset,
    val invertSpectrum: Boolean,
    val rtlAgc: Boolean,
    val ppm: Int,
    /** Record raw IQ during digital sessions (capped by [recordMaxMinutes]). Never for voice. */
    val recordIq: Boolean = false,
    val recordMaxMinutes: Int = 10,
    /** Write the decoder's detailed internal log to diagnostics (large). */
    val verboseLog: Boolean = false,
    val rttyReverse: Boolean = false,
    val rttyShiftHz: Int = 170,
    val fmSquelch: Boolean = true,
    /** Playback volume 0..1 for voice modes. */
    val volume: Float = 0.8f,
    /** -1 lets the RTL-SDR Blog driver pick (Q-branch direct sampling below 24 MHz, tuner above). */
    val directSampling: Int = -1,
    val sampleRateHz: Int = 240_000,
    /** The dongle is tuned this far above the dial so its DC spike stays out of the audio. */
    val captureOffsetHz: Int = 25_000,
    /** js8core SubmodeId bits: Normal, Fast, Turbo, Slow. */
    val submodeMask: Int = 0b01111,
    /** ALE scan dwell per channel. */
    val aleDwellMs: Int = 1000,
    /** Leave 28.1085 MHz out of the scan (the one channel that needs the tuner instead of direct sampling). */
    val aleSkip28: Boolean = false,
    /** JS8: if nothing decodes for 10 min, try the other spectrum orientation once. */
    val invertAuto: Boolean = true,
    /** Set once a JS8 decode has confirmed [invertSpectrum] on this receiver; turns the automatic check off. */
    val invertConfirmed: Boolean = false,
) {
    /** The ALE channels this session scans. */
    val aleChannels: List<Int> get() = preset.aleChannels.filter { !(aleSkip28 && it > 24_000_000) }

    val mode: RxMode get() = preset.mode

    /** 240 kS/s x 2 bytes. */
    val recordMaxBytes: Long get() = recordMaxMinutes * 60L * sampleRateHz * 2

    fun describe(): String = buildString {
        append(preset.label).append(" (").append(mode.label).append(")")
        append(", ppm ").append(ppm)
        append(", RTL AGC ").append(if (rtlAgc) "on" else "off")
        if (invertSpectrum) append(", spectrum inverted")
        if (mode == RxMode.RTTY) append(", RTTY shift ").append(rttyShiftHz).append(" Hz").append(if (rttyReverse) " reversed" else "")
        if (mode == RxMode.JS8 && invertAuto && !invertConfirmed) append(", automatic orientation check")
        if (mode == RxMode.ALE && aleChannels.size > 1) append(", scanning ").append(aleChannels.size)
            .append(" channels, dwell ").append(aleDwellMs).append(" ms (no IQ recording while scanning)")
        if (mode == RxMode.VOICE_FM) append(", squelch ").append(if (fmSquelch) "on" else "off")
        if (recordIq && mode.digital) append(", recording IQ (max ").append(recordMaxMinutes).append(" min)")
        if (verboseLog) append(", verbose decoder log")
    }

    companion object {
        private const val PREFS = "receive_settings"

        fun load(context: Context): ReceiveSettings {
            val p = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            return ReceiveSettings(
                preset = Presets.byId(p.getString("preset", "40m")),
                invertSpectrum = p.getBoolean("invert", false),
                rtlAgc = p.getBoolean("rtl_agc", true),
                ppm = p.getInt("ppm", 0),
                recordIq = p.getBoolean("record_iq", false),
                recordMaxMinutes = p.getInt("record_max_minutes", 10),
                verboseLog = p.getBoolean("verbose_log", false),
                rttyReverse = p.getBoolean("rtty_reverse", false),
                rttyShiftHz = p.getInt("rtty_shift", 170),
                fmSquelch = p.getBoolean("fm_squelch", true),
                volume = p.getFloat("volume", 0.8f),
                aleDwellMs = p.getInt("ale_dwell_ms", 1000),
                aleSkip28 = p.getBoolean("ale_skip_28", false),
                invertAuto = p.getBoolean("invert_auto", true),
                invertConfirmed = p.getBoolean("invert_confirmed", false),
            )
        }

        fun save(context: Context, s: ReceiveSettings) {
            context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
                .putString("preset", s.preset.id)
                .putBoolean("invert", s.invertSpectrum)
                .putBoolean("rtl_agc", s.rtlAgc)
                .putInt("ppm", s.ppm)
                .putBoolean("record_iq", s.recordIq)
                .putInt("record_max_minutes", s.recordMaxMinutes)
                .putBoolean("verbose_log", s.verboseLog)
                .putBoolean("rtty_reverse", s.rttyReverse)
                .putInt("rtty_shift", s.rttyShiftHz)
                .putBoolean("fm_squelch", s.fmSquelch)
                .putFloat("volume", s.volume)
                .putInt("ale_dwell_ms", s.aleDwellMs)
                .putBoolean("ale_skip_28", s.aleSkip28)
                .putBoolean("invert_auto", s.invertAuto)
                .putBoolean("invert_confirmed", s.invertConfirmed)
                .apply()
        }
    }
}

/** App-level preferences that are not receive parameters. */
object AppPrefs {
    private const val PREFS = "app_prefs"

    /** 0 = keep sessions forever. */
    fun retentionDays(c: Context): Int = c.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getInt("retention_days", 0)
    fun setRetentionDays(c: Context, days: Int) =
        c.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putInt("retention_days", days).apply()

    fun groupsOnly(c: Context): Boolean = c.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getBoolean("groups_only", false)
    fun setGroupsOnly(c: Context, v: Boolean) =
        c.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putBoolean("groups_only", v).apply()
}
