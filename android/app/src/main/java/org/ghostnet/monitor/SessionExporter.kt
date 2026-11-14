package org.ghostnet.monitor

import android.content.Context
import android.database.Cursor
import android.net.Uri
import org.json.JSONArray
import org.json.JSONObject
import java.time.Instant
import java.time.ZoneOffset
import java.time.format.DateTimeFormatter

/**
 * Exports one session without modifying it (spec section 8.4): JSON for full
 * fidelity, CSV for spreadsheets, TXT for reading. Every format carries the
 * session metadata, the source booklet version and the receive-only /
 * unauthenticated-content limitations.
 */
object SessionExporter {
    enum class Format(val ext: String, val mime: String) {
        JSON("json", "application/json"),
        CSV("csv", "text/csv"),
        TXT("txt", "text/plain"),
    }

    private val FILE_TIME = DateTimeFormatter.ofPattern("yyyyMMdd'T'HHmmss'Z'").withZone(ZoneOffset.UTC)
    private val ISO = DateTimeFormatter.ISO_INSTANT
    private const val LIMITATIONS =
        "Receive-only monitor. Decoded text is unauthenticated radio traffic; group labels such as @GSTFLASH are as " +
            "received, not verified. Incomplete messages are flagged, not repaired."

    fun suggestedFileName(s: SessionSummary, format: Format): String =
        "GNM_${FILE_TIME.format(Instant.ofEpochMilli(s.startUtcMs))}_${s.protocol}_ALL_${s.id.take(8)}.${format.ext}"

    fun export(context: Context, db: SessionDatabase, session: SessionSummary, format: Format, target: Uri) {
        val body = when (format) {
            Format.JSON -> json(db, session.id)
            Format.CSV -> csv(db, session)
            Format.TXT -> txt(db, session)
        }
        // Write fully, then report; a failed write throws and nothing claims success.
        context.contentResolver.openOutputStream(target, "wt").use { out ->
            requireNotNull(out) { "could not open export destination" }
            out.write(body.toByteArray(Charsets.UTF_8))
            out.flush()
        }
    }

    // ---- JSON -----------------------------------------------------------------

    private fun json(db: SessionDatabase, sessionId: String): String {
        val json = JSONObject()
        json.put("schema_version", "1.0")
        json.put("app", "GhostNet Monitor ${BuildConfigInfo.VERSION}")
        json.put("source_booklet", "S2 Underground, ${GhostNetPlan.SOURCE}")
        json.put("limitations", LIMITATIONS)
        json.put("exported_utc", Instant.now().toString())
        val r = db.readableDatabase
        r.rawQuery("SELECT * FROM sessions WHERE session_id = ?", arrayOf(sessionId)).use {
            require(it.moveToFirst()) { "session $sessionId not found" }
            json.put("session", rowToJson(it))
        }
        json.put("frames", query(r, "SELECT * FROM frames WHERE session_id = ? ORDER BY slot_utc_ms, id", sessionId))
        json.put("messages", query(r, "SELECT * FROM messages WHERE session_id = ? ORDER BY first_utc_ms, id", sessionId))
        return json.toString(2)
    }

    private fun query(db: android.database.sqlite.SQLiteDatabase, sql: String, id: String): JSONArray {
        val arr = JSONArray()
        db.rawQuery(sql, arrayOf(id)).use { c -> while (c.moveToNext()) arr.put(rowToJson(c)) }
        return arr
    }

    private fun rowToJson(c: Cursor): JSONObject {
        val o = JSONObject()
        for (i in 0 until c.columnCount) {
            val name = c.getColumnName(i)
            when (c.getType(i)) {
                Cursor.FIELD_TYPE_NULL -> o.put(name, JSONObject.NULL)
                Cursor.FIELD_TYPE_INTEGER -> o.put(name, c.getLong(i))
                Cursor.FIELD_TYPE_FLOAT -> o.put(name, c.getDouble(i))
                else -> o.put(name, c.getString(i))
            }
        }
        return o
    }

    // ---- CSV ------------------------------------------------------------------

    private fun csvField(v: Any?): String {
        val s = v?.toString() ?: ""
        return if (s.any { it == ',' || it == '"' || it == '\n' || it == '\r' }) "\"" + s.replace("\"", "\"\"") + "\"" else s
    }

    private fun csv(db: SessionDatabase, s: SessionSummary): String = buildString {
        fun row(vararg v: Any?) = appendLine(v.joinToString(",") { csvField(it) })
        appendLine("# GhostNet Monitor ${BuildConfigInfo.VERSION} session export, schema 1.0, ${GhostNetPlan.SOURCE}")
        appendLine("# session ${s.id}, ${s.state}${s.endReason?.let { " ($it)" } ?: ""}, dial ${s.dialHz} Hz, ${s.settings}")
        appendLine("# $LIMITATIONS")
        row("record_type", "utc", "utc_end", "submode", "offset_hz", "snr_db", "status_or_flags", "from", "to",
            "text", "raw")
        for (m in db.messagesFor(s.id)) {
            val status = buildList {
                add(if (m.complete) "complete" else "partial")
                if (m.missingStart) add("missing_start")
                if (m.hasGap) add("missing_frame")
                if (m.timedOut) add("timed_out")
            }.joinToString(" ")
            row("message", ISO.format(Instant.ofEpochMilli(m.firstUtcMs)), ISO.format(Instant.ofEpochMilli(m.lastUtcMs)),
                submodeName(m.submode), "%.1f".format(m.offsetHz), m.snrMaxDb, status, m.from, m.to, m.text, m.rawFrames)
        }
        for (f in db.framesFor(s.id)) {
            row("frame", ISO.format(Instant.ofEpochMilli(f.slotUtcMs)), "", submodeName(f.submode), "%.1f".format(f.offsetHz),
                f.snrDb, "flags=${f.flags} ${f.kind}", f.from, f.to, f.text, f.raw)
        }
    }

    // ---- TXT ------------------------------------------------------------------

    private fun txt(db: SessionDatabase, s: SessionSummary): String = buildString {
        val engine = ScheduleEngine()
        fun t(ms: Long) = "${ISO.format(Instant.ofEpochMilli(ms))} (${Instant.ofEpochMilli(ms).atZone(engine.zone).let {
            DateTimeFormatter.ofPattern("EEE MMM d h:mm:ss a zzz").format(it)
        }})"
        appendLine("GhostNet Monitor ${BuildConfigInfo.VERSION} — session report")
        appendLine("Source plan: S2 Underground, ${GhostNetPlan.SOURCE}")
        appendLine(LIMITATIONS)
        appendLine()
        appendLine("Session:   ${s.id}")
        appendLine("Start:     ${t(s.startUtcMs)}")
        appendLine("End:       ${s.endUtcMs?.let { t(it) } ?: "(still active)"}")
        appendLine("State:     ${s.state}${s.endReason?.let { " ($it)" } ?: ""}")
        appendLine("Protocol:  ${s.protocol}")
        appendLine("Dial:      ${s.dialHz} Hz")
        appendLine("Receiver:  ${s.receiverId}")
        appendLine("Settings:  ${s.settings}")
        appendLine("Decoded:   ${s.frames} frames, ${s.messages} messages")
        s.clockNote?.let { appendLine("Clock:     $it") }
        s.notes?.takeIf { it.isNotBlank() }?.let { appendLine("Notes:     $it") }
        appendLine()
        appendLine("=== Messages ===")
        val messages = db.messagesFor(s.id)
        if (messages.isEmpty()) appendLine("(none)")
        for (m in messages) {
            val flags = buildList {
                if (!m.complete) add("PARTIAL")
                if (m.missingStart) add("start missing")
                if (m.hasGap) add("frame missing")
                if (m.timedOut && !m.complete) add("timed out")
            }
            appendLine("${t(m.firstUtcMs)}  ${submodeName(m.submode)} +${"%.0f".format(m.offsetHz)} Hz  ${m.snrMaxDb} dB" +
                if (flags.isEmpty()) "" else "  [${flags.joinToString(", ")}]")
            appendLine("    ${m.text}")
        }
        appendLine()
        appendLine("=== Raw frames ===")
        val frames = db.framesFor(s.id)
        if (frames.isEmpty()) appendLine("(none)")
        for (f in frames) {
            appendLine("${ISO.format(Instant.ofEpochMilli(f.slotUtcMs))}  ${submodeName(f.submode)} +${"%.0f".format(f.offsetHz)} Hz " +
                "${f.snrDb} dB dt ${"%+.1f".format(f.dtS)}  flags=${f.flags} ${f.kind}  [${f.raw}]  ${f.text}")
        }
    }
}
