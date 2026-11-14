package org.ghostnet.monitor.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.FilterChip
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.delay
import org.ghostnet.monitor.AppPrefs
import org.ghostnet.monitor.FrameRecord
import org.ghostnet.monitor.MessageRecord
import org.ghostnet.monitor.MonitorState
import org.ghostnet.monitor.Presets
import org.ghostnet.monitor.ReceiveSettings
import org.ghostnet.monitor.ScheduleEngine
import org.ghostnet.monitor.submodeName
import java.time.Instant

@Composable
fun ReceiveScreen(controller: AppController) {
    val context = LocalContext.current
    @Suppress("UNUSED_VARIABLE") val tick = controller.tick.intValue
    var clock by remember { mutableIntStateOf(0) }
    LaunchedEffect(Unit) { while (true) { delay(1000); clock++ } }
    @Suppress("UNUSED_EXPRESSION") clock

    var settings by remember { mutableStateOf(ReceiveSettings.load(context)) }
    var groupsOnly by remember { mutableStateOf(AppPrefs.groupsOnly(context)) }
    var showFrames by remember { mutableStateOf(false) }
    val phase = MonitorState.phase
    val busy = phase == MonitorState.Phase.CONNECTING || phase == MonitorState.Phase.READY ||
        phase == MonitorState.Phase.STOPPING || phase == MonitorState.Phase.RECEIVING
    if (!busy) settings = ReceiveSettings.load(context) // picks up changes made on other tabs
    val active = if (busy) MonitorState.settings ?: settings else settings

    Column(Modifier.fillMaxSize().padding(horizontal = 12.dp, vertical = 8.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text("GhostNet Monitor", style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.Bold)
                Text("Receive only · offline · nothing is ever transmitted", style = MaterialTheme.typography.bodySmall)
            }
            PresetPicker(active.preset.id, enabled = !busy) { id ->
                settings = settings.copy(preset = Presets.byId(id))
                ReceiveSettings.save(context, settings)
            }
            Spacer(Modifier.width(8.dp))
            Button(
                onClick = { controller.startStop() },
                enabled = phase != MonitorState.Phase.STOPPING,
                colors = if (phase == MonitorState.Phase.RECEIVING) ButtonDefaults.buttonColors(containerColor = Color(0xFFC62828))
                else ButtonDefaults.buttonColors(),
            ) {
                Text(
                    when (phase) {
                        MonitorState.Phase.RECEIVING -> "STOP"
                        MonitorState.Phase.CONNECTING, MonitorState.Phase.READY -> "CONNECTING…"
                        MonitorState.Phase.STOPPING -> "STOPPING…"
                        else -> "START RECEIVING"
                    },
                )
            }
        }
        if (active.preset.isTest) {
            Text("TEST preset: a busy JS8 calling frequency for checking reception. Not a GhostNet frequency.",
                color = Color(0xFFFFB300), style = MaterialTheme.typography.bodySmall)
        }

        Card(Modifier.fillMaxWidth().padding(top = 8.dp)) {
            Column(Modifier.padding(10.dp)) {
                StatusRow(Status.device(controller.usb))
                StatusRow(Status.signal())
                StatusRow(Status.decoder())
                StatusRow(Status.clock())
                StatusRow(scheduleLine())
                if (active.invertSpectrum || active.recordIq) {
                    Text(
                        listOfNotNull(
                            if (active.invertSpectrum) "Invert spectrum ON" else null,
                            if (active.recordIq) "IQ recording ${if (busy) MonitorState.recordingName ?: "" else "on next start"}" else null,
                        ).joinToString(" · "),
                        style = MaterialTheme.typography.bodySmall,
                    )
                }
            }
        }

        Card(Modifier.fillMaxWidth().height(230.dp).padding(top = 8.dp)) {
            Box(Modifier.padding(8.dp)) { WaterfallView(Modifier.fillMaxSize()) }
        }

        Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.padding(top = 6.dp)) {
            FilterChip(selected = !showFrames, onClick = { showFrames = false }, label = { Text("Messages") })
            Spacer(Modifier.width(6.dp))
            FilterChip(selected = showFrames, onClick = { showFrames = true }, label = { Text("Raw frames") })
            Spacer(Modifier.weight(1f))
            Text("GhostNet groups only", style = MaterialTheme.typography.bodySmall)
            Spacer(Modifier.width(6.dp))
            Switch(checked = groupsOnly, onCheckedChange = { groupsOnly = it; AppPrefs.setGroupsOnly(context, it) })
        }
        Text("Group labels (e.g. @GSTFLASH) are as received and not authenticated. Filtering changes the display only; everything decoded is stored.",
            style = MaterialTheme.typography.labelSmall)

        val messages = MonitorState.messages().map { it.record }.filter { !groupsOnly || isGhostNetGroup(it.to) }
        val frames = MonitorState.frames().map { it.record }
        LazyColumn(Modifier.fillMaxWidth().weight(1f)) {
            if ((showFrames && frames.isEmpty()) || (!showFrames && messages.isEmpty())) {
                item {
                    Text(
                        if (phase == MonitorState.Phase.RECEIVING) "Nothing decoded yet. Decoded traffic appears here."
                        else "Not receiving.",
                        modifier = Modifier.padding(12.dp),
                    )
                }
            }
            if (showFrames) items(frames) { FrameItem(it) } else items(messages) { MessageItem(it) }
        }
    }
}

@Composable
private fun PresetPicker(selectedId: String, enabled: Boolean, onSelect: (String) -> Unit) {
    var open by remember { mutableStateOf(false) }
    Box {
        OutlinedButton(onClick = { open = true }, enabled = enabled) { Text(Presets.byId(selectedId).label) }
        DropdownMenu(expanded = open, onDismissRequest = { open = false }) {
            for (p in Presets.JS8) {
                DropdownMenuItem(
                    text = { Column { Text(p.label); Text(p.purpose, style = MaterialTheme.typography.bodySmall) } },
                    onClick = { open = false; onSelect(p.id) },
                )
            }
        }
    }
}

private fun scheduleLine(): StatusLine {
    val engine = ScheduleEngine()
    val now = Instant.now()
    val active = engine.active(now)
    fun tag(o: ScheduleEngine.Occurrence) = if (o.event.receivableInThisBuild) "" else " (Coming Soon)"
    val text = if (active.isNotEmpty()) {
        active.joinToString("; ") { "NOW ${engine.describe(it)}${tag(it)} · ${engine.localLabel(it)}" }
    } else {
        engine.next(now)?.let { "Next: ${engine.describe(it)}${tag(it)} · ${engine.localLabel(it)} (in ${ScheduleEngine.until(now, it.start)})" } ?: "—"
    }
    return StatusLine("Schedule", if (active.any { it.event.receivableInThisBuild }) Health.OK else Health.IDLE, text)
}

fun isGhostNetGroup(to: String): Boolean = to.startsWith("@GHOSTNET") || to.startsWith("@GSTFLASH") || to.startsWith("@GN")

@Composable
private fun MessageItem(m: MessageRecord) {
    val state = buildList {
        if (!m.complete) add("PARTIAL")
        if (m.missingStart) add("start missing")
        if (m.hasGap) add("frame missing")
        if (m.timedOut && !m.complete) add("timed out")
    }.joinToString(", ")
    Column(Modifier.fillMaxWidth().padding(vertical = 6.dp)) {
        Text(
            "%s (%s local) · %s %+.0f Hz · %d dB%s".format(
                Fmt.utc(m.firstUtcMs), Fmt.local(m.firstUtcMs), submodeName(m.submode), m.offsetHz, m.snrMaxDb,
                if (state.isEmpty()) "" else " · $state",
            ),
            style = MaterialTheme.typography.labelMedium,
        )
        SelectionContainer {
            Text(
                m.text.ifEmpty { "(no text)" },
                fontFamily = FontFamily.Monospace,
                color = if (m.complete) MaterialTheme.colorScheme.onSurface else Color(0xFFFFB300),
                style = MaterialTheme.typography.bodyLarge,
            )
        }
        HorizontalDivider(Modifier.padding(top = 6.dp))
    }
}

@Composable
private fun FrameItem(f: FrameRecord) {
    Text(
        "%s · %s %+.0f Hz %d dB dt%+.1f · %s%s · %s  [%s]".format(
            Fmt.utc(f.slotUtcMs), submodeName(f.submode), f.offsetHz, f.snrDb, f.dtS,
            if (f.flags and 1 != 0) "F" else "-", if (f.flags and 2 != 0) "L" else "-",
            f.text.ifEmpty { "(${f.kind})" }, f.raw,
        ),
        fontFamily = FontFamily.Monospace,
        style = MaterialTheme.typography.bodySmall,
        modifier = Modifier.padding(vertical = 3.dp),
    )
}
