package org.ghostnet.monitor.ui

import androidx.compose.foundation.horizontalScroll
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
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.material3.AssistChip
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.FilterChip
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Slider
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
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
import org.ghostnet.monitor.RxMode
import org.ghostnet.monitor.ScheduleEngine
import org.ghostnet.monitor.submodeName
import java.time.Instant

/** Booklet modes that are visible but not receivable in this build (spec 7.3). */
private val COMING_SOON = listOf("VARA", "Winlink", "DMR", "GhostMesh")

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
    val mode = active.mode

    fun choose(s: ReceiveSettings) {
        settings = s
        ReceiveSettings.save(context, s)
    }

    Column(Modifier.fillMaxSize().padding(horizontal = 12.dp, vertical = 8.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text("GhostNet Monitor", style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.Bold)
                Text("Receive only · offline · nothing is ever transmitted", style = MaterialTheme.typography.bodySmall)
            }
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
                        else -> if (mode.digital) "START RECEIVING" else "START LISTENING"
                    },
                )
            }
        }

        // Mode selector: receivable modes, then Coming Soon (greyed, inert).
        Row(Modifier.horizontalScroll(rememberScrollState()).padding(top = 6.dp), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            for (m in RxMode.values()) {
                FilterChip(
                    selected = mode == m,
                    enabled = !busy,
                    onClick = { if (m != mode) choose(settings.copy(preset = Presets.forMode(m).first())) },
                    label = { Text(m.label) },
                )
            }
            for (name in COMING_SOON) {
                AssistChip(
                    onClick = { controller.toast("$name: Coming Soon. This receive mode has not yet been verified on Android.") },
                    label = { Text("$name · Coming Soon", color = Color.Gray) },
                )
            }
        }

        Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.padding(top = 4.dp)) {
            PresetPicker(active.preset.id, mode, enabled = !busy) { id -> choose(settings.copy(preset = Presets.byId(id))) }
            Spacer(Modifier.width(8.dp))
            Text(active.preset.purpose, style = MaterialTheme.typography.bodySmall, modifier = Modifier.weight(1f))
        }
        if (active.preset.isTest) {
            Text("TEST preset: a busy frequency for checking reception. Not a GhostNet frequency.",
                color = Color(0xFFFFB300), style = MaterialTheme.typography.bodySmall)
        }

        Card(Modifier.fillMaxWidth().padding(top = 8.dp)) {
            Column(Modifier.padding(10.dp)) {
                StatusRow(Status.device(controller.usb))
                StatusRow(Status.signal())
                StatusRow(Status.decoder())
                StatusRow(Status.clock())
                StatusRow(scheduleLine())
                val notes = listOfNotNull(
                    if (active.invertSpectrum && active.preset.dialHz < 24_000_000) "Invert spectrum ON" else null,
                    if (active.recordIq && mode.digital) "IQ recording ${if (busy) MonitorState.recordingName ?: "" else "on next start"}" else null,
                )
                if (notes.isNotEmpty()) Text(notes.joinToString(" · "), style = MaterialTheme.typography.bodySmall)
            }
        }

        Card(Modifier.fillMaxWidth().height(200.dp).padding(top = 8.dp)) {
            Box(Modifier.padding(8.dp)) { WaterfallView(Modifier.fillMaxSize()) }
        }

        when (mode) {
            RxMode.JS8 -> Js8Panel(Modifier.weight(1f), phase, showFrames, groupsOnly, { showFrames = it }) {
                groupsOnly = it
                AppPrefs.setGroupsOnly(context, it)
            }
            RxMode.RTTY -> RttyPanel(Modifier.weight(1f), phase)
            RxMode.ALE -> AlePanel(Modifier.weight(1f), phase, showFrames) { showFrames = it }
            RxMode.VOICE_SSB, RxMode.VOICE_FM -> VoicePanel(Modifier.weight(1f), active, busy) { choose(it) }
        }
    }
}

@Composable
private fun Js8Panel(
    modifier: Modifier, phase: MonitorState.Phase, showFrames: Boolean, groupsOnly: Boolean,
    onShowFrames: (Boolean) -> Unit, onGroupsOnly: (Boolean) -> Unit,
) {
    Column(modifier) {
        Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.padding(top = 6.dp)) {
            FilterChip(selected = !showFrames, onClick = { onShowFrames(false) }, label = { Text("Messages") })
            Spacer(Modifier.width(6.dp))
            FilterChip(selected = showFrames, onClick = { onShowFrames(true) }, label = { Text("Raw frames") })
            Spacer(Modifier.weight(1f))
            Text("GhostNet groups only", style = MaterialTheme.typography.bodySmall)
            Spacer(Modifier.width(6.dp))
            Switch(checked = groupsOnly, onCheckedChange = onGroupsOnly)
        }
        Text("Group labels (e.g. @GSTFLASH) are as received and not authenticated. Filtering changes the display only; everything decoded is stored.",
            style = MaterialTheme.typography.labelSmall)
        val messages = MonitorState.messages().map { it.record }.filter { !groupsOnly || isGhostNetGroup(it.to) }
        val frames = MonitorState.frames().map { it.record }
        LazyColumn(Modifier.fillMaxWidth().weight(1f)) {
            if ((showFrames && frames.isEmpty()) || (!showFrames && messages.isEmpty())) {
                item { EmptyHint(phase) }
            }
            if (showFrames) items(frames) { FrameItem(it) } else items(messages) { MessageItem(it) }
        }
    }
}

@Composable
private fun RttyPanel(modifier: Modifier, phase: MonitorState.Phase) {
    Column(modifier.padding(top = 6.dp)) {
        Text("Receiving now", style = MaterialTheme.typography.labelMedium)
        Card(Modifier.fillMaxWidth()) {
            SelectionContainer {
                Text(
                    MonitorState.rttyLive().ifEmpty { "…" },
                    fontFamily = FontFamily.Monospace,
                    modifier = Modifier.padding(10.dp),
                )
            }
        }
        Text("~ marks a character received with a framing error (shown, never guessed).",
            style = MaterialTheme.typography.labelSmall, modifier = Modifier.padding(vertical = 4.dp))
        val lines = MonitorState.messages().map { it.record }
        LazyColumn(Modifier.fillMaxWidth().weight(1f)) {
            if (lines.isEmpty()) item { EmptyHint(phase) }
            items(lines) { l ->
                Column(Modifier.fillMaxWidth().padding(vertical = 4.dp)) {
                    Text("%s (%s local) · %.0f Hz · %d chars%s".format(Fmt.utc(l.firstUtcMs), Fmt.local(l.firstUtcMs),
                        l.offsetHz, l.frameCount, if (l.complete) "" else " · has framing errors"),
                        style = MaterialTheme.typography.labelMedium)
                    SelectionContainer { Text(l.text, fontFamily = FontFamily.Monospace) }
                    HorizontalDivider(Modifier.padding(top = 4.dp))
                }
            }
        }
    }
}

@Composable
private fun AlePanel(modifier: Modifier, phase: MonitorState.Phase, showWords: Boolean, onShowWords: (Boolean) -> Unit) {
    Column(modifier) {
        Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.padding(top = 6.dp)) {
            FilterChip(selected = !showWords, onClick = { onShowWords(false) }, label = { Text("Calls & soundings") })
            Spacer(Modifier.width(6.dp))
            FilterChip(selected = showWords, onClick = { onShowWords(true) }, label = { Text("Raw words") })
        }
        Text("ALE addresses and AMD text are as received, not verified. Commands other than AMD are logged raw, not interpreted.",
            style = MaterialTheme.typography.labelSmall)
        val tx = MonitorState.messages().map { it.record }
        val words = MonitorState.frames().map { it.record }
        LazyColumn(Modifier.fillMaxWidth().weight(1f)) {
            if ((showWords && words.isEmpty()) || (!showWords && tx.isEmpty())) item { EmptyHint(phase) }
            if (showWords) {
                items(words) { w ->
                    Text("%s · %.4f MHz · %-4s  %s  [%s, %d corrections]".format(Fmt.utc(w.slotUtcMs), w.offsetHz / 1e6,
                        w.kind, w.text.substringAfter(' '), w.raw, w.flags),
                        fontFamily = FontFamily.Monospace, style = MaterialTheme.typography.bodySmall,
                        modifier = Modifier.padding(vertical = 3.dp))
                }
            } else {
                items(tx) { t ->
                    Column(Modifier.fillMaxWidth().padding(vertical = 6.dp)) {
                        Text("%s (%s local) · %.4f MHz · %s · %d words".format(Fmt.utc(t.firstUtcMs), Fmt.local(t.firstUtcMs),
                            t.offsetHz / 1e6, t.rawFrames, t.frameCount), style = MaterialTheme.typography.labelMedium)
                        SelectionContainer {
                            Text(t.text, fontFamily = FontFamily.Monospace,
                                color = if (t.complete) MaterialTheme.colorScheme.onSurface else Color(0xFFFFB300),
                                style = MaterialTheme.typography.bodyLarge)
                        }
                        HorizontalDivider(Modifier.padding(top = 6.dp))
                    }
                }
            }
        }
    }
}

@Composable
private fun VoicePanel(modifier: Modifier, settings: ReceiveSettings, busy: Boolean, onChange: (ReceiveSettings) -> Unit) {
    var volume by remember { mutableFloatStateOf(settings.volume) }
    Column(modifier.padding(top = 8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Card(Modifier.fillMaxWidth()) {
            Column(Modifier.padding(12.dp)) {
                Text("Audio is not recorded or logged", fontWeight = FontWeight.SemiBold)
                Text("Voice plays live through the speaker or headphones only. No session, recording or transcript is kept.",
                    style = MaterialTheme.typography.bodySmall)
            }
        }
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("Volume", modifier = Modifier.width(70.dp))
            Slider(
                value = volume,
                onValueChange = {
                    volume = it
                    MonitorState.volume = it
                },
                onValueChangeFinished = { onChange(settings.copy(volume = volume)) },
                modifier = Modifier.weight(1f),
            )
        }
        if (settings.mode == RxMode.VOICE_FM) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("Squelch (mute when no signal)", modifier = Modifier.weight(1f))
                Switch(checked = settings.fmSquelch, enabled = !busy, onCheckedChange = { onChange(settings.copy(fmSquelch = it)) })
            }
        }
        Text(
            if (settings.mode == RxMode.VOICE_SSB) "7.190 MHz LSB is the booklet's emergency-only voice fallback. The booklet also suggests checking it in the final 5 minutes of regional windows."
            else "VHF/UHF FM simplex is line-of-sight and unscheduled. Above 24 MHz the receiver uses the dongle's normal tuner, not HF direct sampling.",
            style = MaterialTheme.typography.bodySmall,
        )
    }
}

@Composable
private fun EmptyHint(phase: MonitorState.Phase) {
    Text(
        if (phase == MonitorState.Phase.RECEIVING) "Nothing decoded yet. Decoded traffic appears here." else "Not receiving.",
        modifier = Modifier.padding(12.dp),
    )
}

@Composable
private fun PresetPicker(selectedId: String, mode: RxMode, enabled: Boolean, onSelect: (String) -> Unit) {
    var open by remember { mutableStateOf(false) }
    Box {
        OutlinedButton(onClick = { open = true }, enabled = enabled) { Text(Presets.byId(selectedId).label) }
        DropdownMenu(expanded = open, onDismissRequest = { open = false }) {
            for (p in Presets.forMode(mode)) {
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
