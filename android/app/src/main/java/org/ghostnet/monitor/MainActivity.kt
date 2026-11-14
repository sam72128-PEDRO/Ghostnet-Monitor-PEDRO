package org.ghostnet.monitor

import android.Manifest
import android.app.Activity
import android.app.AlertDialog
import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.graphics.Color
import android.graphics.Typeface
import android.hardware.usb.UsbManager
import android.os.Build
import android.os.Bundle
import android.text.InputType
import android.text.SpannableStringBuilder
import android.text.Spanned
import android.text.style.ForegroundColorSpan
import android.view.View
import android.view.ViewGroup
import android.widget.AdapterView
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ListView
import android.widget.Spinner
import android.widget.Switch
import android.widget.TextView
import android.widget.Toast
import java.time.Instant
import java.time.ZoneId
import java.time.ZoneOffset
import java.time.format.DateTimeFormatter
import kotlin.math.abs

/**
 * Milestone-1 operator screen: pick a GhostNet JS8 preset, start/stop,
 * watch Device / Signal / Decoder health separately, and read decoded
 * traffic. No transmit, reply or keying control exists anywhere in the app.
 */
class MainActivity : Activity() {

    private lateinit var usb: UsbManager
    private lateinit var presetSpinner: Spinner
    private lateinit var startStop: Button
    private lateinit var findButton: Button
    private lateinit var selfTestButton: Button
    private lateinit var exportButton: Button
    private lateinit var invertSwitch: Switch
    private lateinit var agcSwitch: Switch
    private lateinit var groupsOnly: Switch
    private lateinit var ppmField: EditText
    private lateinit var deviceStatus: TextView
    private lateinit var signalStatus: TextView
    private lateinit var decoderStatus: TextView
    private lateinit var clockStatus: TextView
    private lateinit var showMessages: Button
    private lateinit var showFrames: Button
    private lateinit var list: ListView
    private lateinit var adapter: ArrayAdapter<CharSequence>

    private var showingFrames = false
    private var startAfterPermission = false
    private var selfTestRunning = false
    private var exportSessionId: String? = null

    private val refresh: () -> Unit = { render() }

    private val permissionReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            if (intent.action != ACTION_USB_PERMISSION) return
            val granted = intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)
            if (granted && startAfterPermission) startMonitoring()
            else if (!granted) toast("USB permission denied. GhostNet Monitor cannot read the receiver without it.")
            startAfterPermission = false
            render()
        }
    }

    override fun onCreate(state: Bundle?) {
        super.onCreate(state)
        usb = getSystemService(UsbManager::class.java)
        buildUi()
        loadSettingsIntoUi()
        registerReceiver(permissionReceiver, IntentFilter(ACTION_USB_PERMISSION), RECEIVER_NOT_EXPORTED)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU &&
            checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED
        ) {
            requestPermissions(arrayOf(Manifest.permission.POST_NOTIFICATIONS), 1)
        }
    }

    override fun onStart() {
        super.onStart()
        MonitorState.observe(refresh)
    }

    override fun onStop() {
        MonitorState.unobserve(refresh)
        super.onStop()
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        render() // e.g. USB_DEVICE_ATTACHED while open
    }

    override fun onDestroy() {
        unregisterReceiver(permissionReceiver)
        super.onDestroy()
    }

    // ---- actions --------------------------------------------------------------

    private fun onStartStop() {
        when (MonitorState.phase) {
            MonitorState.Phase.RECEIVING, MonitorState.Phase.CONNECTING, MonitorState.Phase.READY ->
                startService(Intent(this, MonitorService::class.java).setAction(MonitorService.ACTION_STOP))
            MonitorState.Phase.STOPPING -> Unit
            else -> {
                saveSettingsFromUi()
                val dev = RtlSdrDevices.find(usb)
                when {
                    dev == null -> toast("No RTL-SDR found. Connect it with a USB-C OTG cable or powered hub.")
                    !usb.hasPermission(dev) -> {
                        startAfterPermission = true
                        requestUsbPermission()
                    }
                    else -> startMonitoring()
                }
            }
        }
    }

    private fun startMonitoring() {
        val dev = RtlSdrDevices.find(usb) ?: return toast("Receiver disappeared")
        startForegroundService(
            Intent(this, MonitorService::class.java)
                .setAction(MonitorService.ACTION_START)
                .putExtra(MonitorService.EXTRA_DEVICE_NAME, dev.deviceName),
        )
    }

    private fun requestUsbPermission() {
        val dev = RtlSdrDevices.find(usb) ?: return toast("No RTL-SDR found")
        if (usb.hasPermission(dev)) return toast("Permission already granted for ${RtlSdrDevices.describe(dev)}")
        val pi = PendingIntent.getBroadcast(
            this, 0, Intent(ACTION_USB_PERMISSION).setPackage(packageName),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_MUTABLE,
        )
        usb.requestPermission(dev, pi)
    }

    private fun runSelfTest() {
        selfTestRunning = true
        render()
        toast("Self-test running (about 20 s)…")
        Thread {
            val r = runCatching { NativeReceiver.runSelfTest(4.0) }
            runOnUiThread {
                selfTestRunning = false
                render()
                val msg = r.fold(
                    onSuccess = {
                        (if (it.passed) "PASS\n\n" else "FAIL\n\n") +
                            "Expected: ${it.expectedText}\nDecoded:  ${it.decodedText.ifEmpty { "(nothing)" }}\n\n" +
                            "Frames ${it.framesDecoded}/${it.framesExpected}. %.0f s of capture processed in %.1f s (%.1fx real time; needs > 1x).\n\n%s"
                                .format(it.captureSeconds, it.wallSeconds, it.captureSeconds / it.wallSeconds.coerceAtLeast(0.001) * 1.0, it.detail) +
                            "\n\nThis checks the demodulator and decoder on this tablet with a built-in test signal. It does not test the radio, antenna or tuning."
                    },
                    onFailure = { "Self-test crashed: ${it.message}" },
                )
                AlertDialog.Builder(this).setTitle("Receive chain self-test").setMessage(msg)
                    .setPositiveButton("OK", null).show()
            }
        }.start()
    }

    private fun exportLatest() {
        Thread {
            val db = SessionDatabase(this)
            val id = db.latestSessionId()
            val name = id?.let { SessionExporter.suggestedFileName(db, it) }
            db.close()
            runOnUiThread {
                if (id == null || name == null) return@runOnUiThread toast("No sessions recorded yet")
                exportSessionId = id
                @Suppress("DEPRECATION")
                startActivityForResult(
                    Intent(Intent.ACTION_CREATE_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE)
                        .setType("application/json").putExtra(Intent.EXTRA_TITLE, name),
                    REQUEST_EXPORT,
                )
            }
        }.start()
    }

    @Deprecated("Platform Activity API; no AndroidX in this app")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        val uri = data?.data
        val id = exportSessionId
        if (requestCode != REQUEST_EXPORT || resultCode != RESULT_OK || uri == null || id == null) return
        Thread {
            val db = SessionDatabase(this)
            val result = runCatching { SessionExporter.export(this, db, id, uri) }
            db.close()
            runOnUiThread {
                toast(result.fold({ "Session exported" }, { "Export was not saved: ${it.message}. The session is still stored on this tablet." }))
            }
        }.start()
    }

    // ---- settings -------------------------------------------------------------

    private fun loadSettingsIntoUi() {
        val s = ReceiveSettings.load(this)
        presetSpinner.setSelection(Presets.JS8.indexOf(s.preset).coerceAtLeast(0))
        invertSwitch.isChecked = s.invertSpectrum
        agcSwitch.isChecked = s.rtlAgc
        ppmField.setText(s.ppm.toString())
    }

    private fun saveSettingsFromUi() {
        ReceiveSettings.save(
            this,
            ReceiveSettings(
                preset = Presets.JS8[presetSpinner.selectedItemPosition.coerceIn(0, Presets.JS8.size - 1)],
                invertSpectrum = invertSwitch.isChecked,
                rtlAgc = agcSwitch.isChecked,
                ppm = ppmField.text.toString().toIntOrNull()?.coerceIn(-200, 200) ?: 0,
            ),
        )
    }

    // ---- rendering ------------------------------------------------------------

    private fun render() {
        val phase = MonitorState.phase
        val receiving = phase == MonitorState.Phase.RECEIVING
        val busy = phase == MonitorState.Phase.CONNECTING || phase == MonitorState.Phase.READY ||
            phase == MonitorState.Phase.STOPPING || receiving

        startStop.text = when (phase) {
            MonitorState.Phase.RECEIVING -> "STOP"
            MonitorState.Phase.CONNECTING, MonitorState.Phase.READY -> "CONNECTING…"
            MonitorState.Phase.STOPPING -> "STOPPING…"
            else -> "START RECEIVING"
        }
        startStop.isEnabled = phase != MonitorState.Phase.STOPPING
        for (v in listOf<View>(presetSpinner, invertSwitch, agcSwitch, ppmField)) v.isEnabled = !busy
        selfTestButton.isEnabled = !busy && !selfTestRunning
        findButton.isEnabled = !busy

        renderDevice(phase)
        renderSignalAndDecoder(phase)
        renderList()
    }

    private fun renderDevice(phase: MonitorState.Phase) {
        val dev = RtlSdrDevices.find(usb)
        val (level, text) = when {
            phase == MonitorState.Phase.ERROR -> BAD to "Error: ${MonitorState.error ?: "unknown"}"
            phase == MonitorState.Phase.RECEIVING -> OK to "Receiving · ${MonitorState.deviceText}"
            phase == MonitorState.Phase.CONNECTING || phase == MonitorState.Phase.READY -> WARN to "Connecting…"
            dev == null -> BAD to "No RTL-SDR connected"
            !usb.hasPermission(dev) -> WARN to "${RtlSdrDevices.describe(dev)} · tap Find receiver to grant USB access"
            else -> OK to "${RtlSdrDevices.describe(dev)} · ready"
        }
        status(deviceStatus, "Device", level, text)
    }

    private fun renderSignalAndDecoder(phase: MonitorState.Phase) {
        val s = MonitorState.stats
        if (phase != MonitorState.Phase.RECEIVING || s == null) {
            status(signalStatus, "Signal", IDLE, "—")
            status(decoderStatus, "Decoder", IDLE, "—")
            status(clockStatus, "Clock", IDLE, "UTC ${UTC_TIME.format(Instant.now())} (JS8 needs this within about 1 s)")
            return
        }
        val now = System.currentTimeMillis()
        val iqAge = if (s.lastIqUnixMs > 0) now - s.lastIqUnixMs else Long.MAX_VALUE
        val (sigLevel, sigText) = when {
            iqAge > 3000 -> BAD to "No samples arriving from the receiver"
            s.clipPercent > 1.0 -> WARN to "Overloaded: %.1f%% of samples clipped. Try RTL AGC off or a shorter antenna.".format(s.clipPercent)
            s.iqLevelDbfs < -55 -> WARN to "Very weak input (IQ %.0f dBFS). Check the antenna connection.".format(s.iqLevelDbfs)
            else -> OK to "Samples flowing · IQ %.0f dBFS · channel %.0f dBFS · AGC %+.0f dB".format(
                s.iqLevelDbfs, s.channelLevelDbfs, s.agcGainDb,
            )
        }
        val dropped = if (s.droppedBytes > 0) " · dropped %.1f s of samples".format(s.droppedBytes / 480_000.0) else ""
        status(signalStatus, "Signal", if (dropped.isNotEmpty() && sigLevel == OK) WARN else sigLevel, sigText + dropped)

        val runningMs = now - MonitorState.sessionStartMs
        val (decLevel, decText) = when {
            s.decodePasses == 0L -> WARN to "Waiting for the first 15 s decode window"
            s.frames == 0L && runningMs > 10 * 60_000 ->
                WARN to "No JS8 decoded in ${runningMs / 60_000} min (${s.decodePasses} passes). If a net is active, try Invert spectrum."
            s.frames == 0L -> OK to "Listening · ${s.decodePasses} decode passes · nothing decoded yet"
            else -> OK to "${s.frames} frames · ${s.messages} messages · last ${UTC_TIME.format(Instant.ofEpochMilli(s.lastFrameUnixMs))}"
        }
        status(decoderStatus, "Decoder", decLevel, decText)

        val (clkLevel, clkText) = when {
            s.dtSamples < 3 -> IDLE to "UTC ${UTC_TIME.format(Instant.now())} · timing not yet measured"
            abs(s.medianDtS) > 1.0 -> WARN to "Signals arrive %+.1f s off the slot. The tablet clock is probably wrong; set it to network/GPS time.".format(s.medianDtS)
            else -> OK to "Timing good (median DT %+.2f s over %d decodes)".format(s.medianDtS, s.dtSamples)
        }
        status(clockStatus, "Clock", clkLevel, clkText)
    }

    private fun renderList() {
        val items: List<CharSequence> = if (showingFrames) {
            MonitorState.frames().map { formatFrame(it.record) }
        } else {
            MonitorState.messages().filter { !groupsOnly.isChecked || isGhostNetGroup(it.record.to) }.map { formatMessage(it.record) }
        }
        adapter.clear()
        if (items.isEmpty()) {
            adapter.add(
                if (MonitorState.phase == MonitorState.Phase.RECEIVING) "Nothing decoded yet. Decoded traffic appears here; nothing is ever transmitted."
                else "Not receiving.",
            )
        } else {
            adapter.addAll(items)
        }
        showMessages.isEnabled = showingFrames
        showFrames.isEnabled = !showingFrames
    }

    private fun formatMessage(m: MessageRecord): CharSequence {
        val state = buildList {
            if (!m.complete) add("PARTIAL")
            if (m.missingStart) add("start missing")
            if (m.hasGap) add("frame missing")
            if (m.timedOut && !m.complete) add("timed out")
        }.joinToString(", ")
        val header = "%s (%s local) · %s %+.0f Hz · %d dB%s".format(
            UTC_TIME.format(Instant.ofEpochMilli(m.firstUtcMs)), LOCAL_TIME.format(Instant.ofEpochMilli(m.firstUtcMs)),
            submodeName(m.submode), m.offsetHz, m.snrMaxDb, if (state.isEmpty()) "" else " · $state",
        )
        val sb = SpannableStringBuilder(header).append('\n')
        val start = sb.length
        sb.append(m.text.ifEmpty { "(no text)" })
        sb.setSpan(ForegroundColorSpan(if (m.complete) Color.WHITE else Color.rgb(230, 190, 90)), start, sb.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        return sb
    }

    private fun formatFrame(f: FrameRecord): CharSequence = "%s · %s %+.0f Hz %d dB dt%+.1f · %s%s · %s  [%s]".format(
        UTC_TIME.format(Instant.ofEpochMilli(f.slotUtcMs)), submodeName(f.submode), f.offsetHz, f.snrDb, f.dtS,
        if (f.flags and 1 != 0) "F" else "-", if (f.flags and 2 != 0) "L" else "-", f.text.ifEmpty { "(${f.kind})" }, f.raw,
    )

    private fun isGhostNetGroup(to: String): Boolean =
        to.startsWith("@GHOSTNET") || to.startsWith("@GSTFLASH") || to.startsWith("@GN")

    private fun status(view: TextView, label: String, level: Int, text: String) {
        val sb = SpannableStringBuilder()
        sb.append("● ")
        sb.setSpan(ForegroundColorSpan(level), 0, 1, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        val word = when (level) { OK -> "OK"; WARN -> "CHECK"; BAD -> "PROBLEM"; else -> "" }
        sb.append("$label${if (word.isEmpty()) "" else " ($word)"}: $text")
        view.text = sb
    }

    private fun toast(msg: String) = Toast.makeText(this, msg, Toast.LENGTH_LONG).show()

    // ---- layout ---------------------------------------------------------------

    private fun buildUi() {
        val pad = dp(12)
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(pad, pad, pad, pad)
            setBackgroundColor(Color.rgb(18, 20, 22))
        }
        root.addView(text("GhostNet Monitor", 24f, bold = true))
        root.addView(text("Receive only · offline · nothing is ever transmitted", 13f, color = Color.rgb(160, 170, 175)))

        presetSpinner = Spinner(this).apply {
            adapter = ArrayAdapter(this@MainActivity, android.R.layout.simple_spinner_dropdown_item, Presets.JS8.map { it.label })
            onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
                override fun onItemSelected(p: AdapterView<*>?, v: View?, pos: Int, id: Long) = saveSettingsFromUi()
                override fun onNothingSelected(p: AdapterView<*>?) = Unit
            }
        }
        startStop = Button(this).apply { setOnClickListener { onStartStop() }; minWidth = dp(200) }
        root.addView(row(weighted(presetSpinner), startStop))

        deviceStatus = text("", 14f)
        signalStatus = text("", 14f)
        decoderStatus = text("", 14f)
        clockStatus = text("", 14f)
        listOf(deviceStatus, signalStatus, decoderStatus, clockStatus).forEach { root.addView(it) }

        invertSwitch = Switch(this).apply { text = "Invert spectrum"; setOnCheckedChangeListener { _, _ -> saveSettingsFromUi() } }
        agcSwitch = Switch(this).apply { text = "RTL AGC"; setOnCheckedChangeListener { _, _ -> saveSettingsFromUi() } }
        ppmField = EditText(this).apply {
            hint = "ppm"
            inputType = InputType.TYPE_CLASS_NUMBER or InputType.TYPE_NUMBER_FLAG_SIGNED
            minWidth = dp(70)
            setOnFocusChangeListener { _, has -> if (!has) saveSettingsFromUi() }
        }
        root.addView(row(invertSwitch, agcSwitch, text("PPM", 14f), ppmField))

        findButton = Button(this).apply { text = "Find receiver"; setOnClickListener { requestUsbPermission(); render() } }
        selfTestButton = Button(this).apply { text = "Self-test"; setOnClickListener { runSelfTest() } }
        exportButton = Button(this).apply { text = "Export last session"; setOnClickListener { exportLatest() } }
        root.addView(row(findButton, selfTestButton, exportButton))

        showMessages = Button(this).apply { text = "Messages"; setOnClickListener { showingFrames = false; render() } }
        showFrames = Button(this).apply { text = "Raw frames"; setOnClickListener { showingFrames = true; render() } }
        groupsOnly = Switch(this).apply { text = "GhostNet groups only"; setOnCheckedChangeListener { _, _ -> render() } }
        root.addView(row(showMessages, showFrames, groupsOnly))
        root.addView(text("Group labels (e.g. @GSTFLASH) are shown as received and are not authenticated. Filtering affects display only; everything decoded is stored.", 11f, color = Color.rgb(140, 150, 155)))

        adapter = object : ArrayAdapter<CharSequence>(this, android.R.layout.simple_list_item_1) {
            override fun getView(position: Int, convertView: View?, parent: ViewGroup): View =
                (super.getView(position, convertView, parent) as TextView).apply {
                    setTextColor(Color.rgb(225, 228, 230))
                    typeface = Typeface.MONOSPACE
                    textSize = 14f
                    setTextIsSelectable(false)
                }
        }
        list = ListView(this).apply { adapter = this@MainActivity.adapter }
        root.addView(list, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f))
        setContentView(root)
    }

    private fun text(s: String, size: Float, bold: Boolean = false, color: Int = Color.rgb(225, 228, 230)) =
        TextView(this).apply {
            text = s
            textSize = size
            setTextColor(color)
            if (bold) setTypeface(typeface, Typeface.BOLD)
            setPadding(0, dp(3), 0, dp(3))
        }

    private fun row(vararg views: View) = LinearLayout(this).apply {
        orientation = LinearLayout.HORIZONTAL
        views.forEach { v -> if (v.layoutParams == null) addView(v) else addView(v, v.layoutParams) }
    }

    private fun weighted(v: View): View = v.apply { layoutParams = LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f) }

    private fun dp(v: Int) = (v * resources.displayMetrics.density).toInt()

    companion object {
        private const val ACTION_USB_PERMISSION = "org.ghostnet.monitor.USB_PERMISSION"
        private const val REQUEST_EXPORT = 42
        private val OK = Color.rgb(90, 200, 120)
        private val WARN = Color.rgb(235, 180, 60)
        private val BAD = Color.rgb(230, 80, 70)
        private val IDLE = Color.rgb(120, 125, 130)
        private val UTC_TIME = DateTimeFormatter.ofPattern("HH:mm:ss'Z'").withZone(ZoneOffset.UTC)
        private val LOCAL_TIME = DateTimeFormatter.ofPattern("HH:mm").withZone(ZoneId.systemDefault())
    }
}
