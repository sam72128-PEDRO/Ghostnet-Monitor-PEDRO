package org.ghostnet.monitor

import android.content.Context
import android.net.Uri
import java.io.File
import java.time.Instant
import java.time.ZoneOffset
import java.time.format.DateTimeFormatter
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

/**
 * Raw IQ recordings (.cu8 + .cu8.meta), kept in app-specific storage. They
 * replay exactly through `ghostnet-rx --iq-file`, which is how field
 * problems get debugged offline. 240 kS/s IQ is about 29 MB per minute.
 */
object Recordings {
    private val NAME_TIME = DateTimeFormatter.ofPattern("yyyyMMdd'T'HHmmss'Z'").withZone(ZoneOffset.UTC)

    fun dir(c: Context): File = File(c.getExternalFilesDir(null) ?: c.filesDir, "recordings").apply { mkdirs() }

    fun newFile(c: Context, dialHz: Int): File =
        File(dir(c), "GNM_${NAME_TIME.format(Instant.now())}_${dialHz}Hz.cu8")

    fun list(c: Context): List<File> =
        dir(c).listFiles { f -> f.name.endsWith(".cu8") }?.sortedByDescending { it.lastModified() } ?: emptyList()

    fun totalBytes(c: Context): Long = dir(c).listFiles()?.sumOf { it.length() } ?: 0L

    fun delete(f: File) {
        f.delete()
        File(f.path + ".meta").delete()
    }

    /** One zip holding the capture and its metadata, written to a user-chosen document. */
    fun exportZip(c: Context, f: File, target: Uri) {
        c.contentResolver.openOutputStream(target, "wt").use { raw ->
            requireNotNull(raw) { "could not open export destination" }
            ZipOutputStream(raw.buffered()).use { zip ->
                for (part in listOf(f, File(f.path + ".meta"))) {
                    if (!part.exists()) continue
                    zip.putNextEntry(ZipEntry(part.name))
                    part.inputStream().use { it.copyTo(zip, 1 shl 16) }
                    zip.closeEntry()
                }
                zip.putNextEntry(ZipEntry("README.txt"))
                zip.write(
                    ("Raw RTL-SDR IQ recorded by GhostNet Monitor: interleaved unsigned 8-bit I/Q (.cu8).\n" +
                        "The .meta file gives the dial, capture centre, sample rate and UTC start time.\n" +
                        "Replay: ghostnet-rx --iq-file ${f.name}\n").toByteArray(),
                )
                zip.closeEntry()
            }
        }
    }
}
