package org.ghostnet.monitor.ui

import android.content.Intent
import android.net.Uri
import androidx.core.content.FileProvider
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.ghostnet.monitor.DiagLog
import org.ghostnet.monitor.DiagnosticsExporter
import org.ghostnet.monitor.MessageRecord
import org.ghostnet.monitor.Presets
import org.ghostnet.monitor.Recordings
import org.ghostnet.monitor.ReportExporter
import org.ghostnet.monitor.SessionDatabase
import org.ghostnet.monitor.SessionExporter
import org.ghostnet.monitor.SessionSummary
import java.io.File

enum class ReportAction { SHARE, SAVE }

/**
 * Spec section 8.4: past sessions (including zero-decode ones) with
 * JSON / CSV / TXT export, raw IQ recordings, and the diagnostics report.
 * Exporting never modifies stored data.
 */
@Composable
fun ExportScreen(controller: AppController) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    @Suppress("UNUSED_VARIABLE") val tick = controller.tick.intValue
    var reload by remember { mutableIntStateOf(0) }
    var sessions by remember { mutableStateOf<List<SessionSummary>>(emptyList()) }
    var recordings by remember { mutableStateOf<List<File>>(emptyList()) }
    var viewing by remember { mutableStateOf<Pair<SessionSummary, List<MessageRecord>>?>(null) }
    var pendingSession by remember { mutableStateOf<SessionSummary?>(null) }
    var pendingFormat by remember { mutableStateOf(SessionExporter.Format.JSON) }
    var pendingRecording by remember { mutableStateOf<File?>(null) }
    var confirmDelete by remember { mutableStateOf<File?>(null) }

    LaunchedEffect(reload, controller.tick.intValue / 20) {
        val (s, r) = withContext(Dispatchers.IO) {
            val db = SessionDatabase(context)
            val list = db.listSessions()
            db.close()
            list to Recordings.list(context)
        }
        sessions = s
        recordings = r
    }

    fun runExport(what: String, block: () -> Unit) {
        scope.launch {
            val result = withContext(Dispatchers.IO) { runCatching(block) }
            result.onFailure { DiagLog.e("export", "$what export failed", it) }
            controller.toast(result.fold({ "$what exported" }, { "Export was not saved: ${it.message}. The original stays on this tablet." }))
        }
    }

    val sessionLaunchers = SessionExporter.Format.values().associateWith { format ->
        rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument(format.mime)) { uri: Uri? ->
            val s = pendingSession
            if (uri != null && s != null) runExport("Session") {
                val db = SessionDatabase(context)
                try { SessionExporter.export(context, db, s, format, uri) } finally { db.close() }
            }
        }
    }
    val recordingLauncher = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("application/zip")) { uri: Uri? ->
        val f = pendingRecording
        if (uri != null && f != null) runExport("Recording") { Recordings.exportZip(context, f, uri) }
    }
    val diagnosticsLauncher = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("text/plain")) { uri: Uri? ->
        if (uri != null) runExport("Diagnostics") { DiagnosticsExporter.export(context, uri) }
    }

    // ---- problem report ----
    var askRecording by remember { mutableStateOf<Pair<ReportAction, File>?>(null) }
    var reportAction by remember { mutableStateOf(ReportAction.SHARE) }
    var reportWithRecording by remember { mutableStateOf(false) }
    val reportSaveLauncher = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("application/zip")) { uri: Uri? ->
        if (uri != null) runExport("Report") {
            context.contentResolver.openOutputStream(uri, "wt").use { out ->
                requireNotNull(out) { "could not open export destination" }
                ReportExporter.write(context, out, reportWithRecording)
            }
        }
    }
    fun finishReport(action: ReportAction, withRecording: Boolean) {
        reportWithRecording = withRecording
        when (action) {
            ReportAction.SAVE -> reportSaveLauncher.launch(ReportExporter.fileName())
            ReportAction.SHARE -> scope.launch {
                controller.toast("Preparing report…")
                val result = withContext(Dispatchers.IO) { runCatching { ReportExporter.writeForSharing(context, withRecording) } }
                result.onSuccess { f ->
                    val uri = FileProvider.getUriForFile(context, "org.ghostnet.monitor.reports", f)
                    val send = Intent(Intent.ACTION_SEND)
                        .setType("application/zip")
                        .putExtra(Intent.EXTRA_STREAM, uri)
                        .putExtra(Intent.EXTRA_SUBJECT, "GhostNet Monitor report")
                        .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
                    context.startActivity(Intent.createChooser(send, "Send report"))
                }.onFailure {
                    DiagLog.e("report", "report failed", it)
                    controller.toast("Report failed: ${it.message}")
                }
            }
        }
    }
    fun startReport(action: ReportAction) {
        reportAction = action
        scope.launch {
            val rec = withContext(Dispatchers.IO) { ReportExporter.recordingForLatestSession(context) }
            if (rec != null) askRecording = action to rec else finishReport(action, false)
        }
    }

    LazyColumn(Modifier.fillMaxSize().padding(horizontal = 12.dp)) {
        item {
            Text("Export", style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.Bold, modifier = Modifier.padding(top = 8.dp))
            SectionCard("Problem report") {
                Text("If anything goes wrong, tap Send report. It bundles everything needed to find the cause into one " +
                    "file: diagnostics, the latest session, and (if you made one) its recording.",
                    style = MaterialTheme.typography.bodySmall)
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Button(onClick = { startReport(ReportAction.SHARE) }) { Text("Send report") }
                    OutlinedButton(onClick = { startReport(ReportAction.SAVE) }) { Text("Save report to file") }
                    TextButton(onClick = { diagnosticsLauncher.launch(DiagnosticsExporter.suggestedFileName()) }) { Text("Diagnostics only") }
                }
            }
            Text("Sessions (${sessions.size})", style = MaterialTheme.typography.titleMedium, modifier = Modifier.padding(top = 8.dp))
            if (sessions.isEmpty()) Text("No sessions yet.", modifier = Modifier.padding(8.dp))
        }
        items(sessions, key = { it.id }) { s ->
            Card(Modifier.fillMaxWidth().padding(vertical = 4.dp)) {
                Column(Modifier.padding(12.dp)) {
                    val label = Presets.JS8.firstOrNull { it.dialHz.toLong() == s.dialHz }?.label ?: "${s.dialHz} Hz"
                    Text("JS8 · $label", fontWeight = FontWeight.SemiBold)
                    Text("${Fmt.LOCAL_DATE_TIME.format(java.time.Instant.ofEpochMilli(s.startUtcMs))} · " +
                        Fmt.UTC_DATE_TIME.format(java.time.Instant.ofEpochMilli(s.startUtcMs)) +
                        (s.endUtcMs?.let { " → ${Fmt.UTC_DATE_TIME.format(java.time.Instant.ofEpochMilli(it))}" } ?: " (active)"),
                        style = MaterialTheme.typography.bodySmall)
                    Text("${s.state}${s.endReason?.let { " ($it)" } ?: ""} · ${s.messages} messages · ${s.frames} frames",
                        style = MaterialTheme.typography.bodySmall)
                    Text(s.settings, style = MaterialTheme.typography.labelSmall)
                    s.clockNote?.let { Text(it, style = MaterialTheme.typography.labelSmall) }
                    Row(horizontalArrangement = Arrangement.spacedBy(6.dp), modifier = Modifier.padding(top = 4.dp)) {
                        OutlinedButton(onClick = {
                            scope.launch {
                                val msgs = withContext(Dispatchers.IO) {
                                    val db = SessionDatabase(context)
                                    try { db.messagesFor(s.id) } finally { db.close() }
                                }
                                viewing = s to msgs
                            }
                        }) { Text("View") }
                        for (format in SessionExporter.Format.values()) {
                            OutlinedButton(onClick = {
                                pendingSession = s
                                pendingFormat = format
                                sessionLaunchers.getValue(format).launch(SessionExporter.suggestedFileName(s, format))
                            }) { Text(format.name) }
                        }
                    }
                }
            }
        }
        item {
            Text("IQ recordings (${recordings.size}, ${Fmt.mb(Recordings.totalBytes(context))})",
                style = MaterialTheme.typography.titleMedium, modifier = Modifier.padding(top = 12.dp))
            Text("Turn on recording in Settings. Each recording replays exactly through the desktop tool, so a failed decode can be debugged offline.",
                style = MaterialTheme.typography.bodySmall)
            if (recordings.isEmpty()) Text("No recordings.", modifier = Modifier.padding(8.dp))
        }
        items(recordings, key = { it.path }) { f ->
            Row(Modifier.fillMaxWidth().padding(vertical = 4.dp), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                Column(Modifier.weight(1f)) {
                    Text(f.name, style = MaterialTheme.typography.bodySmall)
                    Text(Fmt.mb(f.length()), style = MaterialTheme.typography.labelSmall)
                }
                OutlinedButton(onClick = { pendingRecording = f; recordingLauncher.launch(f.nameWithoutExtension + ".zip") }) { Text("Export") }
                TextButton(onClick = { confirmDelete = f }) { Text("Delete") }
            }
        }
    }

    viewing?.let { (s, msgs) ->
        AlertDialog(
            onDismissRequest = { viewing = null },
            confirmButton = { TextButton(onClick = { viewing = null }) { Text("Close") } },
            title = { Text("Session ${s.id.take(8)} · ${msgs.size} messages") },
            text = {
                LazyColumn(Modifier.heightIn(max = 480.dp)) {
                    if (msgs.isEmpty()) item { Text("No messages decoded in this session.") }
                    items(msgs) { m ->
                        Column(Modifier.padding(vertical = 4.dp)) {
                            Text("${Fmt.utc(m.firstUtcMs)} ${if (m.complete) "" else "· PARTIAL"}", style = MaterialTheme.typography.labelSmall)
                            SelectionContainer { Text(m.text, fontFamily = FontFamily.Monospace) }
                        }
                    }
                }
            },
        )
    }

    askRecording?.let { (action, rec) ->
        AlertDialog(
            onDismissRequest = { askRecording = null },
            confirmButton = { TextButton(onClick = { askRecording = null; finishReport(action, true) }) { Text("Include recording") } },
            dismissButton = { TextButton(onClick = { askRecording = null; finishReport(action, false) }) { Text("Without it") } },
            title = { Text("Include the IQ recording?") },
            text = {
                Text("The latest session has a raw recording (${Fmt.mb(rec.length())}). Include it if signals showed on " +
                    "the waterfall but nothing decoded. It may be too large for email; Google Drive or Quick Share work.")
            },
        )
    }

    confirmDelete?.let { f ->
        AlertDialog(
            onDismissRequest = { confirmDelete = null },
            confirmButton = {
                TextButton(onClick = {
                    Recordings.delete(f)
                    DiagLog.i("export", "recording deleted: ${f.name}")
                    confirmDelete = null
                    reload++
                }) { Text("Delete") }
            },
            dismissButton = { TextButton(onClick = { confirmDelete = null }) { Text("Keep") } },
            title = { Text("Delete recording?") },
            text = { Text("${f.name} (${Fmt.mb(f.length())}) will be removed from this tablet. Sessions are not affected.") },
        )
    }
}
