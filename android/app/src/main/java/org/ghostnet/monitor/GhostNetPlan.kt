package org.ghostnet.monitor

import java.time.DayOfWeek
import java.time.LocalTime

/**
 * GhostNet Version 1.5 (S2 Underground) frequency catalog and weekly
 * schedule, transcribed from the technical spec sections 5 and 6, which in
 * turn cite booklet pages 12-18, 30, 33-34. UTC is canonical; local times are
 * only ever derived (see ScheduleEngine).
 *
 * This is bundled, versioned data. Nothing updates it at run time.
 */
object GhostNetPlan {
    const val SOURCE = "GhostNet Version 1.5"

    enum class Mode(val label: String, val receivableInThisBuild: Boolean) {
        JS8("JS8", true),
        RTTY("RTTY 45.45 baud", false),
        LSB_VOICE("LSB voice", false),
        VARA_WINLINK("Winlink P2P / VARA", false),
        ALE_2G("Ion2G 2G ALE", false),
        FM_ANALOG("Analog FM", false),
        DMR("DMR", false),
        DMR_ENCRYPTED("Encrypted DMR", false),
    }

    enum class Coverage { ALL, US_AUS_PACIFIC, EUROPE_AFRICA }

    data class Frequency(
        val id: String,
        val hz: Long,
        val mode: Mode,
        val sideband: String,
        val purpose: String,
        val pages: String,
        val coverage: Coverage = Coverage.ALL,
        val note: String? = null,
    ) {
        val mhzLabel: String get() = "%.4f".format(hz / 1e6).trimEnd('0').let { if (it.endsWith('.')) it + "0" else it }
    }

    /** Spec section 5: 26 distinct exact frequencies. 7.107 MHz appears twice (two modes, one frequency). */
    val FREQUENCIES: List<Frequency> = buildList {
        add(Frequency("hf_3575_js8", 3_575_000, Mode.JS8, "USB", "International bridges (NA-AUS, EUR-AUS)", "14, 16, 18"))
        add(Frequency("hf_7077_rtty", 7_077_000, Mode.RTTY, "USB", "Regional recovery window, 45.45 baud", "13, 15, 17",
            note = "Booklet gives baud rate, not shift; 170 Hz is a provisional default."))
        add(Frequency("hf_7107_js8", 7_107_000, Mode.JS8, "USB", "Thursday regional nets; AUS-PAC bridge", "13-18"))
        add(Frequency("hf_7107_vara", 7_107_000, Mode.VARA_WINLINK, "USB", "Regional Winlink P2P window", "13, 15, 17"))
        add(Frequency("hf_7190_lsb", 7_190_000, Mode.LSB_VOICE, "LSB", "Emergency-only voice fallback", "13, 15, 17"))
        add(Frequency("hf_14107_js8", 14_107_000, Mode.JS8, "USB", "Saturday intercontinental bridges", "14, 16, 18"))

        val ale = listOf(3_598_500L, 7_104_500L, 7_106_500L, 7_108_500L, 10_128_500L,
            14_111_500L, 14_113_500L, 14_115_500L, 18_108_500L, 28_108_500L)
        ale.forEachIndexed { i, hz ->
            add(Frequency("ale_%02d".format(i + 1), hz, Mode.ALE_2G, "USB", "%02dGHOSTNET".format(i + 1), "30",
                note = "Booklet labels the ALE list a working draft; may change."))
        }

        add(Frequency("vhf_dmr", 145_105_000, Mode.DMR, "", "VHF DMR (CC 7, TS 1, TG 1105)", "33"))
        add(Frequency("vhf_fm_us", 146_105_000, Mode.FM_ANALOG, "FM", "VHF analog FM", "33", Coverage.US_AUS_PACIFIC))
        add(Frequency("vhf_fm_eu", 144_605_000, Mode.FM_ANALOG, "FM", "VHF analog FM", "33", Coverage.EUROPE_AFRICA))
        add(Frequency("vhf_dmr_enc_us", 147_105_000, Mode.DMR_ENCRYPTED, "", "VHF encrypted DMR (hypothetical)", "33", Coverage.US_AUS_PACIFIC,
            note = "Not legal amateur-band encryption in the US. Never an enabled option."))
        add(Frequency("vhf_dmr_enc_eu", 145_605_000, Mode.DMR_ENCRYPTED, "", "VHF encrypted DMR (hypothetical)", "33", Coverage.EUROPE_AFRICA,
            note = "Never an enabled option."))
        add(Frequency("uhf_dmr_us", 445_105_000, Mode.DMR, "", "UHF DMR", "33", Coverage.US_AUS_PACIFIC))
        add(Frequency("uhf_dmr_eu", 436_105_000, Mode.DMR, "", "UHF DMR", "33", Coverage.EUROPE_AFRICA))
        add(Frequency("uhf_fm_us", 446_105_000, Mode.FM_ANALOG, "FM", "UHF analog FM", "33", Coverage.US_AUS_PACIFIC))
        add(Frequency("uhf_fm_eu", 437_105_000, Mode.FM_ANALOG, "FM", "UHF analog FM", "33", Coverage.EUROPE_AFRICA))
        add(Frequency("uhf_dmr_enc_us", 447_105_000, Mode.DMR_ENCRYPTED, "", "UHF encrypted DMR (hypothetical)", "33", Coverage.US_AUS_PACIFIC,
            note = "Never an enabled option."))
        add(Frequency("uhf_dmr_enc_eu", 438_105_000, Mode.DMR_ENCRYPTED, "", "UHF encrypted DMR (hypothetical)", "33", Coverage.EUROPE_AFRICA,
            note = "Never an enabled option."))
    }

    /** Band reference only, not an exact dial frequency (spec 5.4). */
    const val GHOSTMESH_REFERENCE = "915 MHz US Meshtastic band, LongFast, slot 20, channel GhostMesh (page 34)"

    val ALE_CHANNEL_SET: List<Frequency> get() = FREQUENCIES.filter { it.mode == Mode.ALE_2G }

    fun frequency(id: String): Frequency = FREQUENCIES.first { it.id == id }

    enum class Category { REGIONAL, BRIDGE }

    enum class Network(val label: String, val category: Category, val filter: String) {
        AUSTRALIA("Australia regional", Category.REGIONAL, "Australia"),
        EUROPE("Europe regional", Category.REGIONAL, "Europe"),
        NORTH_AMERICA("North America regional", Category.REGIONAL, "North America"),
        AUS_SOUTH_PACIFIC("Australia - South Pacific bridge", Category.BRIDGE, "Bridges"),
        NA_AUSTRALIA("North America - Australia bridge", Category.BRIDGE, "Bridges"),
        NA_EUROPE("North America - Europe bridge", Category.BRIDGE, "Bridges"),
        EUROPE_AUSTRALIA("Europe - Australia bridge", Category.BRIDGE, "Bridges"),
    }

    /** One weekly window. [frequencyId] is null for ALE windows, which use the whole channel set. */
    data class Event(
        val id: String,
        val network: Network,
        val dayUtc: DayOfWeek,
        val startUtc: LocalTime,
        val endUtc: LocalTime,
        val mode: Mode,
        val frequencyId: String?,
        val pages: String,
        val erratum: String? = null,
    ) {
        val frequencies: List<Frequency> get() = frequencyId?.let { listOf(frequency(it)) } ?: ALE_CHANNEL_SET
        val receivableInThisBuild: Boolean get() = mode.receivableInThisBuild
    }

    private fun regional(prefix: String, network: Network, day: DayOfWeek, startHour: Int, page: String): List<Event> {
        val t = { h: Int, m: Int -> LocalTime.of(h, m) }
        return listOf(
            Event("${prefix}_js8", network, day, t(startHour, 0), t(startHour, 30), Mode.JS8, "hf_7107_js8", page),
            Event("${prefix}_vara", network, day, t(startHour, 30), t(startHour + 1, 0), Mode.VARA_WINLINK, "hf_7107_vara", page,
                erratum = if (network == Network.AUSTRALIA) {
                    "Page 17 narrative says handshakes start at 2035Z, contradicting this table slot. The table slot is used; no 2035Z session is created."
                } else null),
            Event("${prefix}_rtty", network, day, t(startHour + 1, 0), t(startHour + 1, 30), Mode.RTTY, "hf_7077_rtty", page),
            Event("${prefix}_lsb", network, day, t(startHour + 1, 30), t(startHour + 2, 0), Mode.LSB_VOICE, "hf_7190_lsb", page),
        )
    }

    /** Spec section 6: 12 Thursday regional slots + 11 Saturday bridge slots. */
    val EVENTS: List<Event> = buildList {
        addAll(regional("aus_thu", Network.AUSTRALIA, DayOfWeek.THURSDAY, 7, "17"))
        addAll(regional("eur_thu", Network.EUROPE, DayOfWeek.THURSDAY, 18, "15"))
        // North America's "Thursday evening" net is Friday in UTC.
        addAll(regional("na_thu", Network.NORTH_AMERICA, DayOfWeek.FRIDAY, 1, "13"))

        val sat = DayOfWeek.SATURDAY
        val t = { h: Int, m: Int -> LocalTime.of(h, m) }
        add(Event("aus_sp_20m", Network.AUS_SOUTH_PACIFIC, sat, t(8, 0), t(9, 0), Mode.JS8, "hf_14107_js8", "18"))
        add(Event("aus_sp_40m", Network.AUS_SOUTH_PACIFIC, sat, t(9, 0), t(9, 30), Mode.JS8, "hf_7107_js8", "18"))
        add(Event("aus_sp_ale", Network.AUS_SOUTH_PACIFIC, sat, t(9, 30), t(10, 0), Mode.ALE_2G, null, "18, 30"))

        add(Event("na_aus_20m", Network.NA_AUSTRALIA, sat, t(12, 0), t(13, 0), Mode.JS8, "hf_14107_js8", "14"))
        add(Event("na_aus_ale", Network.NA_AUSTRALIA, sat, t(13, 0), t(13, 30), Mode.ALE_2G, null, "14, 30"))
        add(Event("na_aus_80m", Network.NA_AUSTRALIA, sat, t(13, 30), t(14, 0), Mode.JS8, "hf_3575_js8", "14"))

        add(Event("na_eur_20m", Network.NA_EUROPE, sat, t(18, 0), t(19, 0), Mode.JS8, "hf_14107_js8", "14, 16"))
        add(Event("na_eur_ale", Network.NA_EUROPE, sat, t(19, 0), t(20, 0), Mode.ALE_2G, null, "14, 16, 30"))

        add(Event("eur_aus_20m", Network.EUROPE_AUSTRALIA, sat, t(20, 0), t(21, 0), Mode.JS8, "hf_14107_js8", "16"))
        add(Event("eur_aus_ale", Network.EUROPE_AUSTRALIA, sat, t(21, 0), t(21, 30), Mode.ALE_2G, null, "16, 30"))
        add(Event("eur_aus_80m", Network.EUROPE_AUSTRALIA, sat, t(21, 30), t(22, 0), Mode.JS8, "hf_3575_js8", "16"))
    }

    /**
     * Regional nets: the booklet recommends monitoring 7.190 MHz in the last
     * five minutes of each window. One receiver can't do both, so this is a
     * reminder only; nothing ever retunes on its own.
     */
    const val VOICE_REMINDER = "Booklet: monitor 7.190 MHz LSB in the final 5 minutes of regional windows (manual switch only)."
}
