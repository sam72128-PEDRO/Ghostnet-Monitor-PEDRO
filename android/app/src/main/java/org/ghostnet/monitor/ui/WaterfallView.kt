package org.ghostnet.monitor.ui

import android.graphics.Bitmap
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.unit.IntSize
import kotlinx.coroutines.delay
import org.ghostnet.monitor.MonitorState
import org.ghostnet.monitor.Waterfall

/**
 * Live waterfall of 0-3 kHz above the dial (newest at the top). JS8 signals
 * appear as narrow vertical traces about 50 Hz wide, usually between 500 and
 * 2500 Hz. Yellow ticks along the top mark audio offsets decoded in the
 * last two minutes.
 */
@Composable
fun WaterfallView(modifier: Modifier = Modifier) {
    val bitmap = remember { Bitmap.createBitmap(Waterfall.WIDTH, Waterfall.HEIGHT, Bitmap.Config.ARGB_8888) }
    val image = remember { bitmap.asImageBitmap() }
    val pixels = remember { IntArray(Waterfall.WIDTH * Waterfall.HEIGHT) }
    var shown by remember { mutableLongStateOf(-1L) }

    LaunchedEffect(Unit) {
        while (true) {
            val v = Waterfall.version
            if (v != shown) {
                Waterfall.snapshot(pixels)
                bitmap.setPixels(pixels, 0, Waterfall.WIDTH, 0, 0, Waterfall.WIDTH, Waterfall.HEIGHT)
                shown = v
            }
            delay(80)
        }
    }

    Column(modifier) {
        Canvas(Modifier.fillMaxWidth().weight(1f)) {
            @Suppress("UNUSED_EXPRESSION") shown // read so the canvas redraws when a new row lands
            drawImage(
                image,
                dstSize = IntSize(size.width.toInt(), size.height.toInt()),
                filterQuality = FilterQuality.Low,
            )
            val now = System.currentTimeMillis()
            val maxHz = Waterfall.WIDTH * Waterfall.BIN_HZ
            for (f in MonitorState.frames()) {
                if (now - f.record.receivedUtcMs > 120_000) continue
                val x = (f.record.offsetHz / maxHz * size.width).toFloat()
                drawLine(Color.Yellow, Offset(x, 0f), Offset(x, size.height * 0.08f), strokeWidth = 4f)
            }
        }
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
            for (hz in listOf("0", "500", "1000", "1500", "2000", "2500", "3000 Hz")) {
                Text(hz, style = MaterialTheme.typography.labelSmall)
            }
        }
    }
}
