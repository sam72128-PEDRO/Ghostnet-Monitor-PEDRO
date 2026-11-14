package org.ghostnet.monitor.ui

import android.hardware.usb.UsbManager
import androidx.compose.foundation.background
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.Card
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.MutableIntState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import org.ghostnet.monitor.MonitorState
import org.ghostnet.monitor.NativeReceiver
import org.ghostnet.monitor.RtlSdrDevices
import java.time.Instant
import java.time.ZoneId
import java.time.ZoneOffset
import java.time.format.DateTimeFormatter
import kotlin.math.abs

/** What the Compose screens need from the hosting activity. */
interface AppController {
    val usb: UsbManager
    val tick: MutableIntState
    fun startStop()
    fun requestUsbPermission()
    fun toast(msg: String)
}

@Composable
fun GhostNetTheme(content: @Composable () -> Unit) {
    val scheme = if (isSystemInDarkTheme()) darkColorScheme() else lightColorScheme()
    MaterialTheme(colorScheme = scheme, content = content)
}

enum class Health(val color: Color, val word: String) {
    OK(Color(0xFF4CAF50), "OK"),
    CHECK(Color(0xFFFFB300), "CHECK"),
    PROBLEM(Color(0xFFE53935), "PROBLEM"),
    IDLE(Color(0xFF9E9E9E), ""),
}

data class StatusLine(val label: String, val health: Health, val text: String)

/** Status text and colour together: health is never shown by colour alone. */
@Composable
fun StatusRow(line: StatusLine) {
    Row(verticalAlignment = Alignment.Top, modifier = Modifier.padding(vertical = 2.dp)) {
        Spacer(
            Modifier.padding(top = 6.dp, end = 8.dp).size(10.dp).background(line.health.color, CircleShape),
        )
        Text(
            buildString {
                append(line.label)
                if (line.health.word.isNotEmpty()) append(" (").append(line.health.word).append(")")
                append(": ").append(line.text)
            },
            style = MaterialTheme.typography.bodyMedium,
        )
    }
}

@Composable
fun SectionCard(title: String, modifier: Modifier = Modifier, content: @Composable () -> Unit) {
    Card(modifier.fillMaxWidth().padding(vertical = 6.dp)) {
        Column(Modifier.padding(14.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            Text(title, style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.SemiBold)
            content()
        }
    }
}

object Fmt {
    val UTC_TIME: DateTimeFormatter = DateTimeFormatter.ofPattern("HH:mm:ss'Z'").withZone(ZoneOffset.UTC)
    val LOCAL_TIME: DateTimeFormatter = DateTimeFormatter.ofPattern("h:mm a").withZone(ZoneId.of("America/New_York"))
    val LOCAL_DATE_TIME: DateTimeFormatter = DateTimeFormatter.ofPattern("EEE MMM d, h:mm a zzz").withZone(ZoneId.of("America/New_York"))
    val UTC_DATE_TIME: DateTimeFormatter = DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm'Z'").withZone(ZoneOffset.UTC)

    fun utc(ms: Long): String = UTC_TIME.format(Instant.ofEpochMilli(ms))
    fun local(ms: Long): String = LOCAL_TIME.format(Instant.ofEpochMilli(ms))
    fun mb(bytes: Long): String = "%.1f MB".format(bytes / 1_000_000.0)
}

/** Device / Signal / Decoder / Clock health (spec 7.2), shared by Receive and Settings. */
object Status {
    fun device(usb: UsbManager): StatusLine {
        val phase = MonitorState.phase
        val dev = RtlSdrDevices.find(usb)
        val (h, t) = when {
            phase == MonitorState.Phase.ERROR -> Health.PROBLEM to "Error: ${MonitorState.error ?: "unknown"}"
            phase == MonitorState.Phase.RECEIVING -> Health.OK to "Receiving · ${MonitorState.deviceText}"
            phase == MonitorState.Phase.CONNECTING || phase == MonitorState.Phase.READY -> Health.CHECK to "Connecting…"
            dev == null -> Health.PROBLEM to "No RTL-SDR connected"
            !usb.hasPermission(dev) -> Health.CHECK to "${RtlSdrDevices.describe(dev)} · tap Find receiver to grant USB access"
            else -> Health.OK to "${RtlSdrDevices.describe(dev)} · ready"
        }
        return StatusLine("Device", h, t)
    }

    fun signal(): StatusLine {
        val s = MonitorState.stats
        if (MonitorState.phase != MonitorState.Phase.RECEIVING || s == null) return StatusLine("Signal", Health.IDLE, "—")
        val age = if (s.lastIqUnixMs > 0) System.currentTimeMillis() - s.lastIqUnixMs else Long.MAX_VALUE
        var (h, t) = when {
            age > 3000 -> Health.PROBLEM to "No samples arriving from the receiver"
            s.clipPercent > 1.0 -> Health.CHECK to "Overloaded: %.1f%% of samples clipped. Try RTL AGC off or a shorter antenna.".format(s.clipPercent)
            s.iqLevelDbfs < -55 -> Health.CHECK to "Very weak input (IQ %.0f dBFS). Check the antenna connection.".format(s.iqLevelDbfs)
            else -> Health.OK to "Samples flowing · IQ %.0f dBFS · channel %.0f dBFS · AGC %+.0f dB".format(
                s.iqLevelDbfs, s.channelLevelDbfs, s.agcGainDb,
            )
        }
        if (s.droppedBytes > 0) {
            t += " · dropped %.1f s of samples".format(s.droppedBytes / 480_000.0)
            if (h == Health.OK) h = Health.CHECK
        }
        if (s.recording) t += " · recording ${Fmt.mb(s.recordedBytes)}"
        return StatusLine("Signal", h, t)
    }

    fun decoder(): StatusLine {
        val s = MonitorState.stats
        if (MonitorState.phase != MonitorState.Phase.RECEIVING || s == null) return StatusLine("Decoder", Health.IDLE, "—")
        val running = System.currentTimeMillis() - MonitorState.sessionStartMs
        val (h, t) = when {
            s.decodePasses == 0L -> Health.CHECK to "Waiting for the first 15 s decode window"
            s.frames == 0L && running > 10 * 60_000 ->
                Health.CHECK to "No JS8 decoded in ${running / 60_000} min (${s.decodePasses} passes). If signals show on the waterfall, try Invert spectrum."
            s.frames == 0L -> Health.OK to "Listening · ${s.decodePasses} decode passes · nothing decoded yet"
            else -> Health.OK to "${s.frames} frames · ${s.messages} messages · last ${Fmt.utc(s.lastFrameUnixMs)}"
        }
        return StatusLine("Decoder", h, t)
    }

    fun clock(): StatusLine {
        val s: NativeReceiver.Stats? = MonitorState.stats
        val now = "UTC ${Fmt.utc(System.currentTimeMillis())}"
        return when {
            MonitorState.phase != MonitorState.Phase.RECEIVING || s == null || s.dtSamples < 3 ->
                StatusLine("Clock", Health.IDLE, "$now · JS8 needs the clock within about 1 s of UTC")
            abs(s.medianDtS) > 1.0 -> StatusLine("Clock", Health.CHECK,
                "Signals arrive %+.1f s off the slot. The tablet clock is probably wrong; set it to automatic time.".format(s.medianDtS))
            else -> StatusLine("Clock", Health.OK, "$now · timing good (median DT %+.2f s over %d decodes)".format(s.medianDtS, s.dtSamples))
        }
    }
}
