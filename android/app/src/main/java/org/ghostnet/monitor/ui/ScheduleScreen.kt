package org.ghostnet.monitor.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import org.ghostnet.monitor.DiagLog
import org.ghostnet.monitor.GhostNetPlan
import org.ghostnet.monitor.MonitorState
import org.ghostnet.monitor.Presets
import org.ghostnet.monitor.ReceiveSettings
import org.ghostnet.monitor.ScheduleEngine
import java.time.Duration
import java.time.Instant

/**
 * Spec sections 6-7: every published window (including Coming Soon modes),
 * UTC canonical with Florida local time, region filters, and tap-to-tune for
 * JS8 windows. Nothing ever retunes automatically.
 */
@Composable
fun ScheduleScreen(controller: AppController, onTune: () -> Unit) {
    val context = LocalContext.current
    @Suppress("UNUSED_VARIABLE") val tick = controller.tick.intValue
    val engine = remember { ScheduleEngine() }
    var filter by rememberSaveable { mutableStateOf("All") }
    var showCatalog by rememberSaveable { mutableStateOf(false) }
    val now = Instant.now()
    val occurrences = engine.occurrences(now.minus(Duration.ofHours(1)), now.plus(Duration.ofDays(8)))
        .filter { it.end.isAfter(now) }
        .filter { filter == "All" || it.event.network.filter == filter }
    val receiving = MonitorState.phase != MonitorState.Phase.IDLE && MonitorState.phase != MonitorState.Phase.ERROR

    LazyColumn(Modifier.fillMaxSize().padding(horizontal = 12.dp)) {
        item {
            Text("GhostNet schedule", style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.Bold,
                modifier = Modifier.padding(top = 8.dp))
            Text("${GhostNetPlan.SOURCE}. UTC is authoritative; local times are America/New_York with daylight saving applied.",
                style = MaterialTheme.typography.bodySmall)
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp), modifier = Modifier.padding(vertical = 6.dp)) {
                for (f in listOf("All", "North America", "Europe", "Australia", "Bridges")) {
                    FilterChip(selected = filter == f, onClick = { filter = f }, label = { Text(f) })
                }
            }
            Text(GhostNetPlan.VOICE_REMINDER, style = MaterialTheme.typography.bodySmall)
        }
        items(occurrences, key = { it.event.id + it.start.toEpochMilli() }) { o ->
            val isNow = o.isActive(now)
            val receivable = o.event.receivableInThisBuild
            Card(
                Modifier.fillMaxWidth().padding(vertical = 4.dp).clickable(enabled = receivable) {
                    val preset = o.event.frequencyId?.let { Presets.forDial(GhostNetPlan.frequency(it).hz) }
                    when {
                        preset == null -> controller.toast("No receive preset for this window")
                        receiving -> controller.toast("Stop the current session before changing frequency")
                        else -> {
                            ReceiveSettings.save(context, ReceiveSettings.load(context).copy(preset = preset))
                            DiagLog.i("ui", "schedule: preset ${preset.id} selected from ${o.event.id}")
                            controller.toast("Tuned to ${preset.label}. Press START RECEIVING.")
                            onTune()
                        }
                    }
                },
                colors = if (isNow) CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.primaryContainer)
                else CardDefaults.cardColors(),
            ) {
                Column(Modifier.padding(12.dp)) {
                    Row {
                        Text(engine.localLabel(o), fontWeight = FontWeight.SemiBold, modifier = Modifier.weight(1f))
                        Text(
                            when {
                                isNow -> "NOW"
                                else -> "in ${ScheduleEngine.until(now, o.start)}"
                            },
                            fontWeight = FontWeight.SemiBold,
                        )
                    }
                    Text(engine.utcLabel(o), style = MaterialTheme.typography.bodySmall)
                    Text(engine.describe(o))
                    Text(
                        if (receivable) "Tap to set this frequency on Receive"
                        else "Coming Soon: this receive mode has not yet been verified on Android",
                        color = if (receivable) MaterialTheme.colorScheme.primary else Color.Gray,
                        style = MaterialTheme.typography.bodySmall,
                    )
                    o.event.erratum?.let { Text("Source erratum: $it", style = MaterialTheme.typography.bodySmall, color = Color(0xFFFFB300)) }
                    Text("Booklet page ${o.event.pages}", style = MaterialTheme.typography.labelSmall)
                }
            }
        }
        item {
            TextButton(onClick = { showCatalog = !showCatalog }) {
                Text(if (showCatalog) "Hide published frequency list" else "Show all published frequencies")
            }
        }
        if (showCatalog) {
            items(GhostNetPlan.FREQUENCIES) { f ->
                Column(Modifier.padding(vertical = 3.dp)) {
                    Text("${f.mhzLabel} MHz ${f.sideband} · ${f.mode.label}${if (f.mode.receivableInThisBuild) "" else " (Coming Soon)"}")
                    Text(
                        listOfNotNull(
                            f.purpose,
                            when (f.coverage) {
                                GhostNetPlan.Coverage.US_AUS_PACIFIC -> "USA / Australia / Pacific"
                                GhostNetPlan.Coverage.EUROPE_AFRICA -> "Europe / Africa"
                                else -> null
                            },
                            "page ${f.pages}",
                            f.note,
                        ).joinToString(" · "),
                        style = MaterialTheme.typography.bodySmall,
                    )
                }
            }
            item {
                Text("GhostMesh: ${GhostNetPlan.GHOSTMESH_REFERENCE} (Coming Soon)", style = MaterialTheme.typography.bodySmall,
                    modifier = Modifier.padding(vertical = 8.dp))
            }
        }
    }
}
