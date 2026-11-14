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
 * Writes one session (metadata, every raw frame, assembled messages) as
 * JSON to a user-chosen document. Read-only on the database: exporting never
 * modifies stored sessions.
 */
object SessionExporter {
    private val FILE_TIME = DateTimeFormatter.ofPattern("yyyyMMdd'T'HHmmss'Z'").withZone(ZoneOffset.UTC)

    fun suggestedFileName(db: SessionDatabase, sessionId: String): String {
        val start = db.readableDatabase.rawQuery(
            "SELECT start_utc_ms FROM sessions WHERE session_id = ?", arrayOf(sessionId),
        ).use { if (it.moveToFirst()) it.getLong(0) else System.currentTimeMillis() }
        return "GNM_${FILE_TIME.format(Instant.ofEpochMilli(start))}_JS8_ALL_${sessionId.take(8)}.json"
    }

    fun export(context: Context, db: SessionDatabase, sessionId: String, target: Uri) {
        val json = JSONObject()
        json.put("schema_version", "1.0")
        json.put("app", "GhostNet Monitor ${BuildConfigInfo.VERSION}")
        json.put("source_booklet", "S2 Underground, GhostNet Version 1.5")
        json.put(
            "limitations",
            "Receive-only monitor. Decoded text is unauthenticated radio traffic; group labels such as " +
                "@GSTFLASH are as received, not verified. Incomplete messages are flagged, not repaired.",
        )
        json.put("exported_utc", Instant.now().toString())

        val r = db.readableDatabase
        r.rawQuery("SELECT * FROM sessions WHERE session_id = ?", arrayOf(sessionId)).use {
            require(it.moveToFirst()) { "session $sessionId not found" }
            json.put("session", rowToJson(it))
        }
        json.put("frames", query(r, "SELECT * FROM frames WHERE session_id = ? ORDER BY slot_utc_ms, id", sessionId))
        json.put("messages", query(r, "SELECT * FROM messages WHERE session_id = ? ORDER BY first_utc_ms, id", sessionId))

        // Write fully, then report; a failed write throws and nothing claims success.
        context.contentResolver.openOutputStream(target, "wt").use { out ->
            requireNotNull(out) { "could not open export destination" }
            out.write(json.toString(2).toByteArray(Charsets.UTF_8))
            out.flush()
        }
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
}
