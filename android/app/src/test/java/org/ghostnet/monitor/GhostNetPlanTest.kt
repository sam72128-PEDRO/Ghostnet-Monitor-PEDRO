package org.ghostnet.monitor

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.time.DayOfWeek
import java.time.Duration
import java.time.Instant
import java.time.ZonedDateTime

/** Spec section 11.3 frequency and schedule acceptance checks, plus DST fixtures. */
class GhostNetPlanTest {
    private val engine = ScheduleEngine()

    @Test
    fun catalogHasExactly26DistinctExactFrequencies() {
        val distinct = GhostNetPlan.FREQUENCIES.map { it.hz }.toSet()
        assertEquals(26, distinct.size)
        val expected = setOf(
            3_575_000L, 7_077_000L, 7_107_000L, 7_190_000L, 14_107_000L,
            3_598_500L, 7_104_500L, 7_106_500L, 7_108_500L, 10_128_500L,
            14_111_500L, 14_113_500L, 14_115_500L, 18_108_500L, 28_108_500L,
            145_105_000L, 146_105_000L, 147_105_000L, 445_105_000L, 446_105_000L, 447_105_000L,
            144_605_000L, 145_605_000L, 436_105_000L, 437_105_000L, 438_105_000L,
        )
        assertEquals(expected, distinct)
        assertTrue(GhostNetPlan.GHOSTMESH_REFERENCE.contains("915"))
    }

    @Test
    fun sevenOneOhSevenIsOneFrequencyWithTwoModeAssignments() {
        val uses = GhostNetPlan.FREQUENCIES.filter { it.hz == 7_107_000L }
        assertEquals(setOf(GhostNetPlan.Mode.JS8, GhostNetPlan.Mode.VARA_WINLINK), uses.map { it.mode }.toSet())
    }

    @Test
    fun aleListHasTenChannelsWithBookletLabels() {
        val ale = GhostNetPlan.ALE_CHANNEL_SET
        assertEquals(10, ale.size)
        assertEquals("01GHOSTNET", ale.first().purpose)
        assertEquals(28_108_500L, ale.last().hz)
        assertEquals("10GHOSTNET", ale.last().purpose)
    }

    @Test
    fun scheduleHas12ThursdayRegionalAnd11SaturdayBridgeSlots() {
        val regional = GhostNetPlan.EVENTS.filter { it.network.category == GhostNetPlan.Category.REGIONAL }
        val bridges = GhostNetPlan.EVENTS.filter { it.network.category == GhostNetPlan.Category.BRIDGE }
        assertEquals(12, regional.size)
        assertEquals(11, bridges.size)
        assertTrue(bridges.all { it.dayUtc == DayOfWeek.SATURDAY })
        // North America's Thursday-evening net is Friday in UTC.
        assertTrue(regional.filter { it.network == GhostNetPlan.Network.NORTH_AMERICA }.all { it.dayUtc == DayOfWeek.FRIDAY })
        assertTrue(regional.filter { it.network != GhostNetPlan.Network.NORTH_AMERICA }.all { it.dayUtc == DayOfWeek.THURSDAY })
    }

    @Test
    fun windowsWithinANetworkNeverOverlap() {
        for ((_, events) in GhostNetPlan.EVENTS.groupBy { it.network }) {
            val sorted = events.sortedBy { it.startUtc }
            sorted.zipWithNext().forEach { (a, b) -> assertFalse("${a.id} overlaps ${b.id}", a.endUtc.isAfter(b.startUtc)) }
        }
    }

    @Test
    fun receivableModesMatchTheBuildAndEveryModeStaysOnTheSchedule() {
        val receivable = GhostNetPlan.Mode.values().filter { it.receivableInThisBuild }.toSet()
        assertEquals(setOf(GhostNetPlan.Mode.JS8, GhostNetPlan.Mode.RTTY, GhostNetPlan.Mode.LSB_VOICE,
            GhostNetPlan.Mode.FM_ANALOG, GhostNetPlan.Mode.ALE_2G), receivable)
        assertTrue(GhostNetPlan.EVENTS.any { it.mode == GhostNetPlan.Mode.VARA_WINLINK && !it.receivableInThisBuild })
        // ALE windows use the whole channel list: the scan preset covers exactly the catalog's ALE set.
        assertEquals(GhostNetPlan.ALE_CHANNEL_SET.map { it.hz.toInt() }, Presets.byId("alescan").aleChannels)
        assertEquals(RxMode.ALE, Presets.byId("alescan").mode)
        // Every receivable single-frequency window maps to a preset the app can tune.
        for (e in GhostNetPlan.EVENTS.filter { it.receivableInThisBuild && it.frequencyId != null }) {
            val hz = GhostNetPlan.frequency(e.frequencyId!!).hz
            assertNotNull("no preset for ${e.id}", Presets.forDial(hz))
        }
        assertEquals(RxMode.RTTY, Presets.forDial(7_077_000)!!.mode)
        assertEquals(RxMode.VOICE_SSB, Presets.forDial(7_190_000)!!.mode)
        assertNotNull(GhostNetPlan.EVENTS.first { it.id == "aus_thu_vara" }.erratum)
    }

    @Test
    fun ghostNetPresetsMatchCatalogJs8Frequencies() {
        val js8 = GhostNetPlan.FREQUENCIES.filter { it.mode == GhostNetPlan.Mode.JS8 }.map { it.hz.toInt() }.toSet()
        val presets = Presets.JS8.filterNot { it.isTest }.map { it.dialHz }.toSet()
        assertEquals(js8, presets)
        // Test presets must not collide with GhostNet frequencies.
        assertTrue(Presets.JS8.filter { it.id.startsWith("test") }.none { it.dialHz in js8 })
    }

    private fun naNetStartLocal(utcFriday: String): ZonedDateTime {
        val from = Instant.parse(utcFriday).minus(Duration.ofHours(1))
        val occ = engine.occurrences(from, from.plus(Duration.ofHours(2))).first { it.event.id == "na_thu_js8" }
        return engine.localStart(occ)
    }

    @Test
    fun northAmericaNetIsThursdayEveningInFloridaInWinterAndSummer() {
        val winter = naNetStartLocal("2027-01-15T01:00:00Z")
        assertEquals(DayOfWeek.THURSDAY, winter.dayOfWeek)
        assertEquals(20, winter.hour) // 8:00 PM EST
        val summer = naNetStartLocal("2027-07-16T01:00:00Z")
        assertEquals(DayOfWeek.THURSDAY, summer.dayOfWeek)
        assertEquals(21, summer.hour) // 9:00 PM EDT
    }

    @Test
    fun localTimesFollowDstTransitions() {
        // DST ends Sun 2026-11-01; begins Sun 2027-03-14 (America/New_York).
        assertEquals(21, naNetStartLocal("2026-10-30T01:00:00Z").hour)
        assertEquals(20, naNetStartLocal("2026-11-06T01:00:00Z").hour)
        assertEquals(20, naNetStartLocal("2027-03-12T01:00:00Z").hour)
        assertEquals(21, naNetStartLocal("2027-03-19T01:00:00Z").hour)
    }

    @Test
    fun weekHas23OccurrencesAndLabelsAreReadable() {
        val start = Instant.parse("2027-01-11T00:00:00Z") // a Monday
        val week = engine.occurrences(start, start.plus(Duration.ofDays(7)))
        assertEquals(23, week.size)
        val na = week.first { it.event.id == "na_thu_js8" }
        assertEquals("Thu 8:00 PM-8:30 PM EST", engine.localLabel(na))
        assertEquals("Fri 01:00-01:30Z", engine.utcLabel(na))
    }

    @Test
    fun activeAndNextAreFoundAroundAWindow() {
        val during = Instant.parse("2027-01-16T12:30:00Z") // Sat, NA-AUS 20 m bridge
        assertEquals(listOf("na_aus_20m"), engine.active(during).map { it.event.id })
        val nextAny = engine.next(during)!!
        assertEquals("na_aus_ale", nextAny.event.id)
        // ALE is receivable now, so it is also the next receivable window.
        assertEquals("na_aus_ale", engine.next(during, receivableOnly = true)!!.event.id)
    }
}
