package org.ghostnet.monitor

import android.content.Context
import android.util.Log
import java.io.File
import java.io.FileWriter
import java.io.PrintWriter
import java.io.StringWriter
import java.time.Instant
import java.time.ZoneOffset
import java.time.format.DateTimeFormatter

/**
 * Persistent diagnostics log: everything needed to work out what went wrong
 * after the fact. App and service events, USB events, native driver and
 * decoder output, periodic receiver statistics, errors and crashes. It is
 * written to app-private storage, rotated at about 4 MB (two files kept),
 * and leaves the device only through Export diagnostics.
 *
 * It includes raw decoded frames (radio traffic, for debugging) but nothing
 * secret: no accounts, keys or contacts.
 */
object DiagLog {
    enum class Level(val letter: Char) { DEBUG('D'), INFO('I'), WARN('W'), ERROR('E') }

    private const val MAX_BYTES = 4L * 1024 * 1024
    private const val RECENT = 500
    private val TIME = DateTimeFormatter.ofPattern("yyyy-MM-dd'T'HH:mm:ss.SSS'Z'").withZone(ZoneOffset.UTC)

    private val lock = Any()
    private var dir: File? = null
    private val recent = ArrayDeque<String>()

    fun init(context: Context) = synchronized(lock) {
        dir = context.filesDir
    }

    val current: File? get() = dir?.let { File(it, "diagnostics.log") }
    val previous: File? get() = dir?.let { File(it, "diagnostics.1.log") }

    fun d(tag: String, msg: String) = log(Level.DEBUG, tag, msg)
    fun i(tag: String, msg: String) = log(Level.INFO, tag, msg)
    fun w(tag: String, msg: String, t: Throwable? = null) = log(Level.WARN, tag, msg, t)
    fun e(tag: String, msg: String, t: Throwable? = null) = log(Level.ERROR, tag, msg, t)

    fun log(level: Level, tag: String, msg: String, t: Throwable? = null, atMs: Long = System.currentTimeMillis()) {
        val text = if (t == null) msg else msg + "\n" + StringWriter().also { t.printStackTrace(PrintWriter(it)) }
        val line = "${TIME.format(Instant.ofEpochMilli(atMs))} ${level.letter}/$tag: $text"
        when (level) {
            Level.DEBUG -> Log.d("GNM/$tag", text)
            Level.INFO -> Log.i("GNM/$tag", text)
            Level.WARN -> Log.w("GNM/$tag", text)
            Level.ERROR -> Log.e("GNM/$tag", text)
        }
        synchronized(lock) {
            recent.addLast(line)
            while (recent.size > RECENT) recent.removeFirst()
            val f = current ?: return
            try {
                if (f.length() > MAX_BYTES) {
                    previous?.delete()
                    f.renameTo(previous!!)
                }
                FileWriter(f, true).use { it.write(line); it.write("\n") }
            } catch (e: Exception) {
                Log.e("GNM/DiagLog", "cannot write diagnostics log", e)
            }
        }
    }

    /** Native lines arrive as "unixMs|source|text". */
    fun native(line: String) {
        val parts = line.split('|', limit = 3)
        if (parts.size == 3) {
            val level = if (parts[2].contains("error", ignoreCase = true) || parts[2].contains("fail", ignoreCase = true)) Level.WARN else Level.INFO
            log(level, "native.${parts[1]}", parts[2], atMs = parts[0].toLongOrNull() ?: System.currentTimeMillis())
        } else {
            log(Level.INFO, "native", line)
        }
    }

    fun recentLines(): List<String> = synchronized(lock) { recent.toList() }

    fun clear() = synchronized(lock) {
        recent.clear()
        current?.delete()
        previous?.delete()
    }
}
