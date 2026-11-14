package org.ghostnet.monitor

import android.net.Uri
import androidx.test.core.app.ApplicationProvider
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import java.io.File

/** Session storage, the three session export formats, retention, and the diagnostics report. */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [34])
class DataExportTest {
    private val context = ApplicationProvider.getApplicationContext<android.content.Context>()

    private fun seed(db: SessionDatabase, endMs: Long): SessionSummary {
        val settings = ReceiveSettings(Presets.byId("40m"), invertSpectrum = true, rtlAgc = true, ppm = 0)
        val id = db.startSession(settings, "RTLSDRBlog Blog V3 00000001", endMs - 60_000)
        db.addFrame(id, FrameRecord(endMs - 60_000, endMs - 45_000, 0, 1500.0, -12, 0.1f, 1, "compound", "AY-pe-ukujxW", "KN4CRD:", "KN4CRD", ""))
        db.addMessage(id, MessageRecord(endMs - 60_000, endMs - 15_000, 0, 1500.0, -14, -12, 4, true, false, false, false,
            "KN4CRD: @GHOSTNET TEST, WITH \"QUOTES\"", "KN4CRD", "@GHOSTNET", "EL96", "AY-pe-ukujxW x y z"))
        db.endSession(id, "Completed", "stop", endMs, "Median JS8 DT +0.10 s over 4 decodes", "decode passes 8")
        return db.listSessions().first { it.id == id }
    }

    private fun export(db: SessionDatabase, s: SessionSummary, format: SessionExporter.Format): String {
        val f = File(context.cacheDir, "export.${format.ext}")
        SessionExporter.export(context, db, s, format, Uri.fromFile(f))
        return f.readText()
    }

    @Test
    fun sessionExportsCarryTheDataAndMetadata() {
        val db = SessionDatabase(context)
        val s = seed(db, System.currentTimeMillis())
        assertEquals(1, s.frames)
        assertEquals(1, s.messages)

        val json = JSONObject(export(db, s, SessionExporter.Format.JSON))
        assertEquals("Completed", json.getJSONObject("session").getString("state"))
        assertEquals(1, json.getJSONArray("frames").length())
        assertTrue(json.getJSONArray("messages").getJSONObject(0).getString("text").contains("@GHOSTNET"))
        assertTrue(json.getString("limitations").contains("unauthenticated"))

        val csv = export(db, s, SessionExporter.Format.CSV)
        assertTrue(csv.contains("\"KN4CRD: @GHOSTNET TEST, WITH \"\"QUOTES\"\"\"")) // RFC 4180 quoting
        assertTrue(csv.lines().any { it.startsWith("frame,") })

        val txt = export(db, s, SessionExporter.Format.TXT)
        assertTrue(txt.contains("=== Messages ==="))
        assertTrue(txt.contains("spectrum inverted"))
        assertTrue(SessionExporter.suggestedFileName(s, SessionExporter.Format.CSV).matches(Regex("GNM_\\d{8}T\\d{6}Z_JS8_ALL_[0-9a-f]{8}\\.csv")))
        db.close()
    }

    @Test
    fun retentionRemovesOnlyOldFinishedSessions() {
        val db = SessionDatabase(context)
        val now = System.currentTimeMillis()
        seed(db, now - 40L * 86_400_000)
        val recent = seed(db, now)
        val active = db.startSession(ReceiveSettings(Presets.byId("40m"), false, true, 0), "x", now - 50L * 86_400_000)
        assertEquals(1, db.deleteSessionsEndedBefore(now - 30L * 86_400_000))
        val ids = db.listSessions().map { it.id }
        assertEquals(2, ids.size)
        assertTrue(recent.id in ids)
        assertTrue(active in ids) // never delete an Active session
        assertTrue(db.framesFor(recent.id).isNotEmpty()) // others' frames untouched
        db.close()
    }

    @Test
    fun diagnosticsReportCoversTheEssentials() {
        DiagLog.init(context)
        DiagLog.i("test", "marker line for diagnostics")
        val db = SessionDatabase(context)
        seed(db, System.currentTimeMillis())
        db.close()
        val report = DiagnosticsExporter.build(context)
        for (section in listOf("===== Device =====", "===== USB devices =====", "===== Settings =====",
            "===== Recent sessions =====", "===== Previous process exits =====", "===== Diagnostics log =====",
            "===== Logcat (this app only) =====")) {
            assertTrue("missing $section", report.contains(section))
        }
        assertTrue(report.contains("marker line for diagnostics"))
        assertTrue(report.contains("Completed (stop)"))
    }
}
