package org.ghostnet.monitor

import java.time.Duration
import java.time.Instant
import java.time.LocalDate
import java.time.ZoneId
import java.time.ZoneOffset
import java.time.ZonedDateTime
import java.time.format.DateTimeFormatter
import java.util.Locale

/**
 * Turns the weekly UTC schedule into concrete occurrences. UTC stays
 * canonical; local times are derived per occurrence through the OS time-zone
 * database, so daylight-saving changes are handled without any table of
 * offsets.
 */
class ScheduleEngine(
    val zone: ZoneId = ZoneId.of("America/New_York"),
    private val events: List<GhostNetPlan.Event> = GhostNetPlan.EVENTS,
) {
    data class Occurrence(val event: GhostNetPlan.Event, val start: Instant, val end: Instant) {
        fun isActive(now: Instant) = !now.isBefore(start) && now.isBefore(end)
    }

    /** All occurrences overlapping [from, to), ordered by start. */
    fun occurrences(from: Instant, to: Instant): List<Occurrence> {
        val firstDay = from.atZone(ZoneOffset.UTC).toLocalDate().minusDays(1)
        val lastDay = to.atZone(ZoneOffset.UTC).toLocalDate().plusDays(1)
        val out = mutableListOf<Occurrence>()
        var day = firstDay
        while (!day.isAfter(lastDay)) {
            for (e in events) {
                if (e.dayUtc != day.dayOfWeek) continue
                val start = day.atTime(e.startUtc).toInstant(ZoneOffset.UTC)
                var end = day.atTime(e.endUtc).toInstant(ZoneOffset.UTC)
                if (!end.isAfter(start)) end = end.plus(Duration.ofDays(1))
                if (end.isAfter(from) && start.isBefore(to)) out += Occurrence(e, start, end)
            }
            day = day.plusDays(1)
        }
        return out.sortedWith(compareBy({ it.start }, { it.event.id }))
    }

    fun active(now: Instant): List<Occurrence> = occurrences(now, now.plusSeconds(1)).filter { it.isActive(now) }

    /** Next occurrence starting after [now], optionally only receivable (JS8) ones. */
    fun next(now: Instant, receivableOnly: Boolean = false): Occurrence? =
        occurrences(now, now.plus(Duration.ofDays(8)))
            .firstOrNull { it.start.isAfter(now) && (!receivableOnly || it.event.receivableInThisBuild) }

    fun localStart(o: Occurrence): ZonedDateTime = o.start.atZone(zone)
    fun localEnd(o: Occurrence): ZonedDateTime = o.end.atZone(zone)

    /** e.g. "Thu 8:00 PM-8:30 PM EST". The weekday is the local one, so NA's net reads as Thursday. */
    fun localLabel(o: Occurrence): String {
        val s = localStart(o)
        val e = localEnd(o)
        return "%s %s-%s %s".format(DAY.format(s), TIME.format(s), TIME.format(e), ZONE.format(s))
    }

    fun utcLabel(o: Occurrence): String {
        val s = o.start.atZone(ZoneOffset.UTC)
        val e = o.end.atZone(ZoneOffset.UTC)
        return "%s %s-%sZ".format(DAY.format(s), UTC_TIME.format(s), UTC_TIME.format(e))
    }

    fun describe(o: Occurrence): String {
        val freq = o.event.frequencyId?.let { GhostNetPlan.frequency(it).mhzLabel + " MHz" } ?: "10-channel ALE list"
        return "${o.event.network.label} · ${o.event.mode.label} $freq"
    }

    companion object {
        private val DAY = DateTimeFormatter.ofPattern("EEE", Locale.US)
        private val TIME = DateTimeFormatter.ofPattern("h:mm a", Locale.US)
        private val ZONE = DateTimeFormatter.ofPattern("zzz", Locale.US)
        private val UTC_TIME = DateTimeFormatter.ofPattern("HH:mm", Locale.US)

        fun until(now: Instant, then: Instant): String {
            val m = Duration.between(now, then).toMinutes().coerceAtLeast(0)
            return when {
                m >= 24 * 60 -> "${m / (24 * 60)} d ${(m / 60) % 24} h"
                m >= 60 -> "${m / 60} h ${m % 60} min"
                else -> "$m min"
            }
        }
    }
}
