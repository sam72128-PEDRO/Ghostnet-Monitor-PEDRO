package org.ghostnet.monitor.ui

import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.ghostnet.monitor.AppPrefs
import org.ghostnet.monitor.BuildConfigInfo
import org.ghostnet.monitor.DiagLog
import org.ghostnet.monitor.DiagnosticsExporter
import org.ghostnet.monitor.GhostNetPlan
import org.ghostnet.monitor.MonitorState
import org.ghostnet.monitor.NativeReceiver
import org.ghostnet.monitor.ReceiveSettings
import org.ghostnet.monitor.Recordings

@Composable
fun SettingsScreen(controller: AppController) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    @Suppress("UNUSED_VARIABLE") val tick = controller.tick.intValue
    var s by remember { mutableStateOf(ReceiveSettings.load(context)) }
    var retention by remember { mutableStateOf(AppPrefs.retentionDays(context)) }
    var selfTest by remember { mutableStateOf<String?>(null) }
    var selfTestRunning by remember { mutableStateOf(false) }
    var confirmClear by remember { mutableStateOf(false) }
    val idle = MonitorState.phase == MonitorState.Phase.IDLE || MonitorState.phase == MonitorState.Phase.ERROR

    fun update(n: ReceiveSettings) {
        s = n
        ReceiveSettings.save(context, n)
        DiagLog.i("settings", n.describe())
    }

    val diagnosticsLauncher = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("text/plain")) { uri: Uri? ->
        if (uri != null) scope.launch {
            val r = withContext(Dispatchers.IO) { runCatching { DiagnosticsExporter.export(context, uri) } }
            controller.toast(r.fold({ "Diagnostics exported" }, { "Export failed: ${it.message}" }))
        }
    }

    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 12.dp)) {
        Text("Settings", style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.Bold, modifier = Modifier.padding(top = 8.dp))
        if (!idle) Text("Receive settings apply when a session starts; stop receiving to change them.",
            style = MaterialTheme.typography.bodySmall)

        SectionCard("Receiver (RTL-SDR Blog V3)") {
            StatusRow(Status.device(controller.usb))
            OutlinedButton(onClick = { controller.requestUsbPermission() }, enabled = idle) { Text("Find receiver / grant USB access") }
            Toggle("Invert spectrum (HF)", "Flip if signals show on the waterfall but nothing decodes. Applies below 24 MHz, where Q-branch direct sampling orientation is unverified on this hardware.",
                s.invertSpectrum, idle) { update(s.copy(invertSpectrum = it)) }
            Toggle("RTL AGC", "RTL2832U digital gain control. Turn off if the Signal line reports overload.",
                s.rtlAgc, idle) { update(s.copy(rtlAgc = it)) }
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("Frequency correction (ppm)", modifier = Modifier.weight(1f))
                OutlinedButton(onClick = { update(s.copy(ppm = (s.ppm - 1).coerceAtLeast(-200))) }, enabled = idle) { Text("−") }
                Text(" ${s.ppm} ", modifier = Modifier.padding(horizontal = 8.dp))
                OutlinedButton(onClick = { update(s.copy(ppm = (s.ppm + 1).coerceAtMost(200))) }, enabled = idle) { Text("+") }
            }
            Text("The V3 has a 1 ppm TCXO; 0 is normally right.", style = MaterialTheme.typography.bodySmall)
        }

        SectionCard("RTTY") {
            Toggle("Reverse (swap mark and space)", "Try this if RTTY tones are found but the text is garbage.",
                s.rttyReverse, idle) { update(s.copy(rttyReverse = it)) }
            Text("Shift", style = MaterialTheme.typography.bodyMedium)
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                for (shift in listOf(170, 425, 850)) {
                    FilterChip(selected = s.rttyShiftHz == shift, enabled = idle,
                        onClick = { update(s.copy(rttyShiftHz = shift)) }, label = { Text("$shift Hz") })
                }
            }
            Text("The booklet gives 45.45 baud but not the shift; 170 Hz is the amateur standard. The tone pair is found automatically.",
                style = MaterialTheme.typography.bodySmall)
        }

        SectionCard("IQ recording") {
            Toggle("Record raw IQ during digital sessions", "JS8 and RTTY only (voice is never recorded). Saves exactly what the receiver delivered, about 29 MB per minute, so problems can be replayed and debugged offline.",
                s.recordIq, idle) { update(s.copy(recordIq = it)) }
            Text("Stop recording after", style = MaterialTheme.typography.bodyMedium)
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                for (m in listOf(5, 10, 20, 30)) {
                    FilterChip(selected = s.recordMaxMinutes == m, enabled = idle,
                        onClick = { update(s.copy(recordMaxMinutes = m)) }, label = { Text("$m min") })
                }
            }
            Text("Recordings on this tablet: ${Recordings.list(context).size}, ${Fmt.mb(Recordings.totalBytes(context))}. Export or delete them on the Export tab.",
                style = MaterialTheme.typography.bodySmall)
        }

        SectionCard("Diagnostics") {
            Toggle("Verbose decoder log", "Adds the JS8 decoder's internal timing log to diagnostics. Larger, but useful if decoding fails.",
                s.verboseLog, idle) { update(s.copy(verboseLog = it)) }
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Button(onClick = {
                    selfTestRunning = true
                    DiagLog.i("selftest", "started")
                    scope.launch {
                        val r = withContext(Dispatchers.Default) { runCatching { NativeReceiver.runSelfTest(4.0) } }
                        selfTestRunning = false
                        selfTest = r.fold(
                            onSuccess = {
                                val rt = it.captureSeconds / it.wallSeconds.coerceAtLeast(0.001)
                                val text = (if (it.passed) "PASS\n\n" else "FAIL\n\n") +
                                    "Expected: ${it.expectedText}\nDecoded:  ${it.decodedText.ifEmpty { "(nothing)" }}\n\n" +
                                    "Frames ${it.framesDecoded}/${it.framesExpected}. %.0f s of capture processed in %.1f s (%.1fx real time; needs well above 1x).\n\n%s"
                                        .format(it.captureSeconds, it.wallSeconds, rt, it.detail) +
                                    "\n\nThis checks the demodulator and decoder on this tablet with a built-in test signal. It does not test the radio, antenna or tuning."
                                DiagLog.log(if (it.passed) DiagLog.Level.INFO else DiagLog.Level.ERROR, "selftest", text.replace("\n", " | "))
                                text
                            },
                            onFailure = {
                                DiagLog.e("selftest", "crashed", it)
                                "Self-test crashed: ${it.message}"
                            },
                        )
                    }
                }, enabled = idle && !selfTestRunning) { Text(if (selfTestRunning) "Self-test running (~20 s)…" else "Run self-test") }
                OutlinedButton(onClick = { diagnosticsLauncher.launch(DiagnosticsExporter.suggestedFileName()) }) { Text("Export diagnostics") }
                TextButton(onClick = { confirmClear = true }) { Text("Clear log") }
            }
            Text("Recent log", style = MaterialTheme.typography.bodyMedium)
            Text(DiagLog.recentLines().takeLast(12).joinToString("\n").ifEmpty { "(empty)" }, style = MaterialTheme.typography.labelSmall)
        }

        SectionCard("Data") {
            Text("Keep sessions", style = MaterialTheme.typography.bodyMedium)
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                for ((days, label) in listOf(0 to "Forever", 90 to "90 days", 30 to "30 days")) {
                    FilterChip(selected = retention == days, onClick = {
                        retention = days
                        AppPrefs.setRetentionDays(context, days)
                        DiagLog.i("settings", "retention ${if (days == 0) "forever" else "$days days"}")
                    }, label = { Text(label) })
                }
            }
            Text("Older finished sessions are removed when the app starts. Analog audio is never recorded or logged.",
                style = MaterialTheme.typography.bodySmall)
        }

        SectionCard("About") {
            Text("GhostNet Monitor ${BuildConfigInfo.VERSION}. Receive-only: no transmission, keying, replies or network access. " +
                "Schedule and frequencies from S2 Underground, ${GhostNetPlan.SOURCE}.", style = MaterialTheme.typography.bodySmall)
            Text("Receives via RTL-SDR: JS8, RTTY, LSB voice and VHF/UHF FM voice (none yet verified on this hardware). " +
                "Coming Soon: 2G ALE, DigiRig input, VARA/Winlink, DMR, GhostMesh.",
                style = MaterialTheme.typography.bodySmall)
            Text("Open-source components: js8core (MIT) · rtl-sdr-blog librtlsdr (GPL-2.0, locally patched for Android) · " +
                "libusb (LGPL-2.1) · Eigen (MPL-2.0) · CRCpp (BSD-3-Clause) · Jetpack Compose and AndroidX (Apache-2.0). " +
                "Decoded content is unauthenticated radio traffic.", style = MaterialTheme.typography.bodySmall)
        }
        Spacer(Modifier.padding(12.dp))
    }

    selfTest?.let { text ->
        AlertDialog(
            onDismissRequest = { selfTest = null },
            confirmButton = { TextButton(onClick = { selfTest = null }) { Text("OK") } },
            title = { Text("Receive chain self-test") },
            text = { Text(text) },
        )
    }
    if (confirmClear) {
        AlertDialog(
            onDismissRequest = { confirmClear = false },
            confirmButton = { TextButton(onClick = { DiagLog.clear(); DiagLog.i("settings", "diagnostics log cleared"); confirmClear = false }) { Text("Clear") } },
            dismissButton = { TextButton(onClick = { confirmClear = false }) { Text("Keep") } },
            title = { Text("Clear diagnostics log?") },
            text = { Text("Only do this after you've exported anything you might need. Sessions and recordings are not affected.") },
        )
    }
}

@Composable
private fun Toggle(title: String, help: String, checked: Boolean, enabled: Boolean, onChange: (Boolean) -> Unit) {
    Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.fillMaxWidth()) {
        Column(Modifier.weight(1f)) {
            Text(title)
            Text(help, style = MaterialTheme.typography.bodySmall)
        }
        Spacer(Modifier.width(8.dp))
        Switch(checked = checked, onCheckedChange = onChange, enabled = enabled)
    }
}
