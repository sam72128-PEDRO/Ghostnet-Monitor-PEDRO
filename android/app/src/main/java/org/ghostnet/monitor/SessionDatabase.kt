package org.ghostnet.monitor

import android.content.ContentValues
import android.content.Context
import android.database.sqlite.SQLiteDatabase
import android.database.sqlite.SQLiteOpenHelper
import java.util.UUID

/**
 * Digital monitoring sessions, every decoded frame (raw modem text always
 * kept), and assembled messages. One row per session even when nothing
 * decoded. App-private; nothing leaves the device except through an
 * explicit user export.
 */
class SessionDatabase(context: Context) : SQLiteOpenHelper(context, "ghostnet_monitor.db", null, 1) {

    override fun onConfigure(db: SQLiteDatabase) {
        db.setForeignKeyConstraintsEnabled(true)
    }

    override fun onCreate(db: SQLiteDatabase) {
        db.execSQL(
            """CREATE TABLE sessions (
                session_id TEXT PRIMARY KEY,
                start_utc_ms INTEGER NOT NULL,
                end_utc_ms INTEGER,
                state TEXT NOT NULL,
                end_reason TEXT,
                protocol TEXT NOT NULL,
                decoder_version TEXT NOT NULL,
                receiver_type TEXT NOT NULL,
                receiver_id TEXT,
                dial_hz INTEGER NOT NULL,
                sideband TEXT NOT NULL,
                tuning_verified INTEGER NOT NULL,
                settings TEXT,
                frame_count INTEGER NOT NULL DEFAULT 0,
                message_count INTEGER NOT NULL DEFAULT 0,
                clock_note TEXT,
                capture_notes TEXT)""",
        )
        db.execSQL(
            """CREATE TABLE frames (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                session_id TEXT NOT NULL REFERENCES sessions(session_id),
                slot_utc_ms INTEGER NOT NULL,
                received_utc_ms INTEGER NOT NULL,
                submode INTEGER NOT NULL,
                offset_hz REAL NOT NULL,
                snr_db INTEGER NOT NULL,
                dt_s REAL NOT NULL,
                flags INTEGER NOT NULL,
                kind TEXT NOT NULL,
                raw TEXT NOT NULL,
                text TEXT,
                from_call TEXT,
                to_call TEXT)""",
        )
        db.execSQL(
            """CREATE TABLE messages (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                session_id TEXT NOT NULL REFERENCES sessions(session_id),
                first_utc_ms INTEGER NOT NULL,
                last_utc_ms INTEGER NOT NULL,
                submode INTEGER NOT NULL,
                offset_hz REAL NOT NULL,
                snr_min_db INTEGER NOT NULL,
                snr_max_db INTEGER NOT NULL,
                frame_count INTEGER NOT NULL,
                complete INTEGER NOT NULL,
                missing_start INTEGER NOT NULL,
                has_gap INTEGER NOT NULL,
                timed_out INTEGER NOT NULL,
                text TEXT NOT NULL,
                from_call TEXT,
                to_call TEXT,
                grid TEXT,
                raw_frames TEXT NOT NULL)""",
        )
        db.execSQL("CREATE INDEX frames_session ON frames(session_id, slot_utc_ms)")
        db.execSQL("CREATE INDEX messages_session ON messages(session_id, first_utc_ms)")
    }

    override fun onUpgrade(db: SQLiteDatabase, oldVersion: Int, newVersion: Int) = Unit

    /** A session left "Active" means the app died mid-session; it must not merge with a new one. */
    fun recoverInterruptedSessions(): Int {
        val db = writableDatabase
        return db.compileStatement(
            """UPDATE sessions SET state = 'Interrupted', end_reason = 'app_terminated',
               end_utc_ms = COALESCE((SELECT MAX(received_utc_ms) FROM frames f WHERE f.session_id = sessions.session_id),
                                     start_utc_ms)
               WHERE state = 'Active'""",
        ).executeUpdateDelete()
    }

    fun startSession(settings: ReceiveSettings, receiverId: String, startUtcMs: Long): String {
        val id = UUID.randomUUID().toString()
        writableDatabase.insertOrThrow("sessions", null, ContentValues().apply {
            put("session_id", id)
            put("start_utc_ms", startUtcMs)
            put("state", "Active")
            put("protocol", settings.mode.protocol)
            put("decoder_version", "js8core 0.1.0 / rx-core ${BuildConfigInfo.VERSION}")
            put("receiver_type", "RTL_SDR_V3")
            put("receiver_id", receiverId)
            put("dial_hz", settings.preset.dialHz)
            put("sideband", if (settings.preset.usb) "USB" else "LSB")
            put("tuning_verified", 1) // the app tunes the SDR itself
            put("settings", settings.describe())
        })
        return id
    }

    fun addFrame(sessionId: String, f: FrameRecord) {
        val db = writableDatabase
        db.insertOrThrow("frames", null, ContentValues().apply {
            put("session_id", sessionId)
            put("slot_utc_ms", f.slotUtcMs)
            put("received_utc_ms", f.receivedUtcMs)
            put("submode", f.submode)
            put("offset_hz", f.offsetHz)
            put("snr_db", f.snrDb)
            put("dt_s", f.dtS.toDouble())
            put("flags", f.flags)
            put("kind", f.kind)
            put("raw", f.raw)
            put("text", f.text)
            put("from_call", f.from)
            put("to_call", f.to)
        })
        db.execSQL("UPDATE sessions SET frame_count = frame_count + 1 WHERE session_id = ?", arrayOf(sessionId))
    }

    fun addMessage(sessionId: String, m: MessageRecord) {
        val db = writableDatabase
        db.insertOrThrow("messages", null, ContentValues().apply {
            put("session_id", sessionId)
            put("first_utc_ms", m.firstUtcMs)
            put("last_utc_ms", m.lastUtcMs)
            put("submode", m.submode)
            put("offset_hz", m.offsetHz)
            put("snr_min_db", m.snrMinDb)
            put("snr_max_db", m.snrMaxDb)
            put("frame_count", m.frameCount)
            put("complete", if (m.complete) 1 else 0)
            put("missing_start", if (m.missingStart) 1 else 0)
            put("has_gap", if (m.hasGap) 1 else 0)
            put("timed_out", if (m.timedOut) 1 else 0)
            put("text", m.text)
            put("from_call", m.from)
            put("to_call", m.to)
            put("grid", m.grid)
            put("raw_frames", m.rawFrames)
        })
        db.execSQL("UPDATE sessions SET message_count = message_count + 1 WHERE session_id = ?", arrayOf(sessionId))
    }

    fun endSession(sessionId: String, state: String, reason: String, endUtcMs: Long, clockNote: String?, notes: String?) {
        writableDatabase.update("sessions", ContentValues().apply {
            put("state", state)
            put("end_reason", reason)
            put("end_utc_ms", endUtcMs)
            put("clock_note", clockNote)
            put("capture_notes", notes)
        }, "session_id = ?", arrayOf(sessionId))
    }

    fun latestSessionId(): String? =
        readableDatabase.rawQuery("SELECT session_id FROM sessions ORDER BY start_utc_ms DESC LIMIT 1", null).use {
            if (it.moveToFirst()) it.getString(0) else null
        }

    fun listSessions(limit: Int = 200): List<SessionSummary> =
        readableDatabase.rawQuery(
            """SELECT session_id, start_utc_ms, end_utc_ms, state, end_reason, dial_hz, receiver_id,
                      frame_count, message_count, settings, clock_note, capture_notes, protocol
               FROM sessions ORDER BY start_utc_ms DESC LIMIT $limit""",
            null,
        ).use { c ->
            buildList {
                while (c.moveToNext()) add(
                    SessionSummary(
                        id = c.getString(0), startUtcMs = c.getLong(1), endUtcMs = if (c.isNull(2)) null else c.getLong(2),
                        state = c.getString(3), endReason = c.getString(4), dialHz = c.getLong(5),
                        receiverId = c.getString(6) ?: "", frames = c.getInt(7), messages = c.getInt(8),
                        settings = c.getString(9) ?: "", clockNote = c.getString(10), notes = c.getString(11),
                        protocol = c.getString(12) ?: "JS8",
                    ),
                )
            }
        }

    fun messagesFor(sessionId: String): List<MessageRecord> =
        readableDatabase.rawQuery(
            """SELECT first_utc_ms, last_utc_ms, submode, offset_hz, snr_min_db, snr_max_db, frame_count, complete,
                      missing_start, has_gap, timed_out, text, from_call, to_call, grid, raw_frames
               FROM messages WHERE session_id = ? ORDER BY first_utc_ms, id""",
            arrayOf(sessionId),
        ).use { c ->
            buildList {
                while (c.moveToNext()) add(
                    MessageRecord(
                        c.getLong(0), c.getLong(1), c.getInt(2), c.getDouble(3), c.getInt(4), c.getInt(5), c.getInt(6),
                        c.getInt(7) != 0, c.getInt(8) != 0, c.getInt(9) != 0, c.getInt(10) != 0,
                        c.getString(11) ?: "", c.getString(12) ?: "", c.getString(13) ?: "", c.getString(14) ?: "",
                        c.getString(15) ?: "",
                    ),
                )
            }
        }

    fun framesFor(sessionId: String): List<FrameRecord> =
        readableDatabase.rawQuery(
            """SELECT slot_utc_ms, received_utc_ms, submode, offset_hz, snr_db, dt_s, flags, kind, raw, text,
                      from_call, to_call FROM frames WHERE session_id = ? ORDER BY slot_utc_ms, id""",
            arrayOf(sessionId),
        ).use { c ->
            buildList {
                while (c.moveToNext()) add(
                    FrameRecord(
                        c.getLong(0), c.getLong(1), c.getInt(2), c.getDouble(3), c.getInt(4), c.getFloat(5), c.getInt(6),
                        c.getString(7) ?: "", c.getString(8) ?: "", c.getString(9) ?: "", c.getString(10) ?: "",
                        c.getString(11) ?: "",
                    ),
                )
            }
        }

    /** Retention: removes finished sessions (and their frames/messages) that ended before [cutoffMs]. */
    fun deleteSessionsEndedBefore(cutoffMs: Long): Int {
        val db = writableDatabase
        db.beginTransaction()
        try {
            // Android binds arguments as text, and COALESCE() strips the column's
            // integer affinity, so without the CAST every number compares less than
            // the text cutoff and *every* finished session would match.
            val doomed = "state != 'Active' AND COALESCE(end_utc_ms, start_utc_ms) < CAST(? AS INTEGER)"
            val args = arrayOf(cutoffMs.toString())
            val n = db.rawQuery("SELECT COUNT(*) FROM sessions WHERE $doomed", args).use { if (it.moveToFirst()) it.getInt(0) else 0 }
            val inDoomed = "session_id IN (SELECT session_id FROM sessions WHERE $doomed)"
            db.delete("frames", inDoomed, args)
            db.delete("messages", inDoomed, args)
            db.delete("sessions", doomed, args)
            db.setTransactionSuccessful()
            return n
        } finally {
            db.endTransaction()
        }
    }

    fun sessionCount(): Int =
        readableDatabase.rawQuery("SELECT COUNT(*) FROM sessions", null).use { if (it.moveToFirst()) it.getInt(0) else 0 }
}

/** Version string recorded with each session (kept in code so it needs no generated BuildConfig). */
object BuildConfigInfo {
    const val VERSION = "0.5"
}

data class SessionSummary(
    val id: String, val startUtcMs: Long, val endUtcMs: Long?, val state: String, val endReason: String?,
    val dialHz: Long, val receiverId: String, val frames: Int, val messages: Int, val settings: String,
    val clockNote: String?, val notes: String?, val protocol: String = "JS8",
)

data class FrameRecord(
    val slotUtcMs: Long, val receivedUtcMs: Long, val submode: Int, val offsetHz: Double, val snrDb: Int,
    val dtS: Float, val flags: Int, val kind: String, val raw: String, val text: String, val from: String, val to: String,
)

data class MessageRecord(
    val firstUtcMs: Long, val lastUtcMs: Long, val submode: Int, val offsetHz: Double, val snrMinDb: Int,
    val snrMaxDb: Int, val frameCount: Int, val complete: Boolean, val missingStart: Boolean, val hasGap: Boolean,
    val timedOut: Boolean, val text: String, val from: String, val to: String, val grid: String, val rawFrames: String,
)

fun submodeName(submode: Int): String = when (submode) {
    0 -> "Normal"
    1 -> "Fast"
    2 -> "Turbo"
    3 -> "Slow"
    4 -> "Ultra"
    else -> "?"
}
