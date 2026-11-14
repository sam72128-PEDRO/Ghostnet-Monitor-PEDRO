package org.ghostnet.monitor

import android.content.Context
import java.io.File
import java.io.OutputStream
import java.time.Instant
import java.time.ZoneOffset
import java.time.format.DateTimeFormatter
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

/**
 * One-tap problem report: a single zip holding the diagnostics, the latest
 * session (JSON for analysis, TXT for reading) and, optionally, that
 * session's IQ recording. The person sending it doesn't need to know which
 * piece is which.
 */
object ReportExporter {
    private val NAME_TIME = DateTimeFormatter.ofPattern("yyyyMMdd'T'HHmmss'Z'").withZone(ZoneOffset.UTC)

    fun fileName(): String = "GNM_report_${NAME_TIME.format(Instant.now())}.zip"

    /** The newest recording made during the latest session, if any. */
    fun recordingForLatestSession(c: Context): File? {
        val db = SessionDatabase(c)
        val latest = try { db.listSessions(1).firstOrNull() } finally { db.close() }
        latest ?: return null
        return Recordings.list(c).firstOrNull { it.lastModified() >= latest.startUtcMs - 5_000 }
    }

    fun write(c: Context, out: OutputStream, includeRecording: Boolean) {
        ZipOutputStream(out.buffered()).use { zip ->
            fun text(name: String, body: String) {
                zip.putNextEntry(ZipEntry(name))
                zip.write(body.toByteArray(Charsets.UTF_8))
                zip.closeEntry()
            }
            text("diagnostics.txt", DiagnosticsExporter.build(c))

            val db = SessionDatabase(c)
            try {
                db.listSessions(1).firstOrNull()?.let { s ->
                    for (format in listOf(SessionExporter.Format.JSON, SessionExporter.Format.TXT)) {
                        val tmp = File.createTempFile("session", "." + format.ext, c.cacheDir)
                        try {
                            SessionExporter.export(c, db, s, format, android.net.Uri.fromFile(tmp))
                            zip.putNextEntry(ZipEntry("latest_session.${format.ext}"))
                            tmp.inputStream().use { it.copyTo(zip) }
                            zip.closeEntry()
                        } finally {
                            tmp.delete()
                        }
                    }
                }
            } finally {
                db.close()
            }

            if (includeRecording) {
                recordingForLatestSession(c)?.let { rec ->
                    for (part in listOf(rec, File(rec.path + ".meta"))) {
                        if (!part.exists()) continue
                        zip.putNextEntry(ZipEntry("recording/${part.name}"))
                        part.inputStream().use { it.copyTo(zip, 1 shl 16) }
                        zip.closeEntry()
                    }
                }
            }

            text(
                "README.txt",
                "GhostNet Monitor problem report.\n" +
                    "diagnostics.txt: device, USB, settings, event log, exit reasons, system log.\n" +
                    "latest_session.json / .txt: the most recent receive session.\n" +
                    (if (includeRecording) "recording/: raw IQ from that session (replay with ghostnet-rx --iq-file).\n" else ""),
            )
        }
        DiagLog.i("report", "report written (recording ${if (includeRecording) "included" else "not included"})")
    }

    /** Writes the report into app cache for sharing; returns the file. */
    fun writeForSharing(c: Context, includeRecording: Boolean): File {
        val dir = File(c.cacheDir, "reports").apply { mkdirs() }
        dir.listFiles()?.forEach { it.delete() } // only ever keep the latest
        val f = File(dir, fileName())
        f.outputStream().use { write(c, it, includeRecording) }
        return f
    }
}
