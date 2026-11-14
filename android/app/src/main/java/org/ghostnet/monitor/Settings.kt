package org.ghostnet.monitor

import android.content.Context

/** A dial frequency the receiver can tune (USB). */
data class Preset(val id: String, val label: String, val dialHz: Int, val purpose: String) {
    val isTest: Boolean get() = id.startsWith("test")
}

object Presets {
    val JS8 = listOf(
        Preset("80m", "80 m · 3.575 MHz USB", 3_575_000, "International bridges (NA-AUS, EUR-AUS)"),
        Preset("40m", "40 m · 7.107 MHz USB", 7_107_000, "Thursday regional nets; AUS-PAC bridge"),
        Preset("20m", "20 m · 14.107 MHz USB", 14_107_000, "Saturday intercontinental bridges"),
        // Not GhostNet frequencies: the standard JS8 calling frequencies, which
        // carry JS8 traffic most hours. Here only to verify reception between
        // scheduled GhostNet windows; labelled so they cannot be mistaken for nets.
        Preset("test40", "TEST · JS8 calling 7.078 MHz", 7_078_000, "Receiver check only, not GhostNet"),
        Preset("test20", "TEST · JS8 calling 14.078 MHz", 14_078_000, "Receiver check only, not GhostNet"),
        Preset("test80", "TEST · JS8 calling 3.578 MHz", 3_578_000, "Receiver check only, not GhostNet"),
    )

    fun byId(id: String?): Preset = JS8.firstOrNull { it.id == id } ?: JS8[1]

    fun forDial(hz: Long): Preset? = JS8.firstOrNull { !it.isTest && it.dialHz.toLong() == hz }
}

data class ReceiveSettings(
    val preset: Preset,
    val invertSpectrum: Boolean,
    val rtlAgc: Boolean,
    val ppm: Int,
    /** Record raw IQ for the whole session (capped by [recordMaxMinutes]). */
    val recordIq: Boolean = false,
    val recordMaxMinutes: Int = 10,
    /** Write the decoder's detailed internal log to diagnostics (large). */
    val verboseLog: Boolean = false,
    /** -1 lets the RTL-SDR Blog driver pick (Q-branch direct sampling below 24 MHz). */
    val directSampling: Int = -1,
    val sampleRateHz: Int = 240_000,
    /** The dongle is tuned this far above the dial so its DC spike stays out of the audio. */
    val captureOffsetHz: Int = 25_000,
    /** js8core SubmodeId bits: Normal, Fast, Turbo, Slow. */
    val submodeMask: Int = 0b01111,
) {
    /** 240 kS/s x 2 bytes. */
    val recordMaxBytes: Long get() = recordMaxMinutes * 60L * sampleRateHz * 2

    fun describe(): String = buildString {
        append(preset.label)
        append(", ppm ").append(ppm)
        append(", RTL AGC ").append(if (rtlAgc) "on" else "off")
        if (invertSpectrum) append(", spectrum inverted")
        if (recordIq) append(", recording IQ (max ").append(recordMaxMinutes).append(" min)")
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
