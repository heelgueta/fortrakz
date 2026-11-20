package app.fortrakz

import android.app.Application
import android.content.Context
import android.content.pm.PackageManager
import android.media.AudioDeviceInfo
import android.media.AudioManager
import android.os.Build
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import org.json.JSONObject
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import kotlin.math.max
import kotlin.math.roundToInt

data class TrackUi(
    val gain: Float = 0.8f,
    val mute: Boolean = false,
    val solo: Boolean = false,
    val hasTake: Boolean = false,
)

data class UiState(
    val pos: Float = 0f,
    val len: Float = 0f,
    val state: Int = 0,           // 0 stopped, 1 playing, 2 recording
    val peaks: List<Float> = listOf(0f, 0f, 0f, 0f),
    val inPeak: Float = 0f,
    val takeSec: Float = 0f,
    val armed: Int = -1,
    val tracks: List<TrackUi> = List(4) { TrackUi() },
    val busy: String? = null,
    val message: String? = null,
    val calibrated: Boolean = false,
    val engineOk: Boolean = false,
    val xruns: Int = 0,
)

data class LabRow(val tag: String, val line1: String, val line2: String, val good: Boolean)
data class LabUi(
    val running: Boolean = false,
    val progress: String = "",
    val rows: List<LabRow> = emptyList(),
    val lastDir: String? = null,
)

val INPUT_PRESETS = listOf(
    9 to "Unprocessed (raw mic)",
    6 to "Voice recognition",
    5 to "Camcorder",
    1 to "Generic",
    7 to "Voice communication (platform echo cancel)",
)

class StudioVM(app: Application) : AndroidViewModel(app) {
    val prefs = Prefs(app)
    var ui by mutableStateOf(UiState())
        private set
    var lab by mutableStateOf(LabUi())
        private set
    var settingsTick by mutableStateOf(0)  // bump to recompose settings after prefs change
        private set

    private val lock = Any()
    private fun upd(f: (UiState) -> UiState) { synchronized(lock) { ui = f(ui) } }

    private var booted = false
    private var finishing = false
    private var msgJob: Job? = null
    private val labRoot: File get() =
        (getApplication<Application>().getExternalFilesDir("lab") ?: File(getApplication<Application>().filesDir, "lab")).apply { mkdirs() }

    // ---------------------------------------------------------------- boot ---

    fun boot() {
        if (booted) return
        booted = true
        val ctx = getApplication<Application>()
        val dir = File(ctx.filesDir, "project").apply { mkdirs() }
        NativeEngine.init(dir.absolutePath)
        val tracks = List(4) { TrackUi(prefs.gain(it), prefs.mute(it), prefs.solo(it), NativeEngine.hasTake(it)) }
        tracks.forEachIndexed { i, t -> NativeEngine.setTrack(i, t.gain, t.mute, t.solo) }
        upd { it.copy(tracks = tracks, calibrated = NativeEngine.echoValid() && prefs.rtlFrames >= 0) }
        applyConfig()
        startEngine()
        viewModelScope.launch(Dispatchers.Default) { poll() }
    }

    private fun applyConfig() {
        NativeEngine.setConfig(prefs.inputPreset, prefs.perfMode)
        NativeEngine.setCleanMode(prefs.cleanMode)
        val rtl = prefs.rtlFrames
        NativeEngine.setLatencyFrames(if (rtl >= 0) max(0, rtl + prefs.trimMs * 48) else 0)
    }

    private fun startEngine() {
        val ok = NativeEngine.startStreams()
        upd { it.copy(engineOk = ok) }
        if (!ok) say("Couldn't open the audio streams. Is the microphone free?")
    }

    fun onBackground() {
        if (!booted || lab.running) return
        if (ui.busy == null) {
            viewModelScope.launch(Dispatchers.Default) {
                endTransport()
                NativeEngine.stopStreams()
                upd { it.copy(engineOk = false) }
            }
        }
    }

    fun onForeground() {
        if (booted && !lab.running && ui.busy == null && !ui.engineOk) viewModelScope.launch(Dispatchers.Default) { startEngine() }
    }

    private suspend fun poll() {
        val b = FloatArray(12)
        var prev = 0
        while (viewModelScope.isActive) {
            NativeEngine.status(b)
            val st = b[2].toInt()
            val takes = List(4) { NativeEngine.hasTake(it) }
            upd { it.copy(
                pos = b[0], len = b[1], state = st,
                peaks = listOf(b[3], b[4], b[5], b[6]), inPeak = b[7], takeSec = b[8],
                xruns = b[10].toInt(),
                tracks = it.tracks.mapIndexed { i, tr -> tr.copy(hasTake = takes[i]) },
            ) }
            if (prev == 2 && st == 0 && !finishing) finishTake()  // hit the 10 minute cap
            prev = st
            delay(50)
        }
    }

    // ------------------------------------------------------------ messages ---

    fun say(text: String) {
        upd { it.copy(message = text) }
        msgJob?.cancel()
        msgJob = viewModelScope.launch { delay(6000); upd { it.copy(message = null) } }
    }

    // ----------------------------------------------------------- transport ---

    fun arm(t: Int) {
        if (ui.state == 2) return
        upd { it.copy(armed = if (it.armed == t) -1 else t) }
    }

    fun rewind() { if (ui.state != 2 && ui.busy == null) NativeEngine.seek(0f) }

    fun play() {
        if (ui.busy != null || !ui.engineOk) return
        if (ui.state == 2) return
        NativeEngine.play()
    }

    fun record() {
        if (ui.busy != null || !ui.engineOk || ui.state == 2) return
        if (ui.armed < 0) { say("Tap the red circle on a track first, then press record."); return }
        if (!ui.calibrated) say("Not calibrated yet: the new track will land a little late. Calibrate in settings.")
        NativeEngine.record(ui.armed)
    }

    fun stop() {
        if (ui.busy != null) return
        viewModelScope.launch(Dispatchers.Default) { endTransport() }
    }

    private fun endTransport() {
        val pending = NativeEngine.stopTransport()
        if (pending && !finishing) finishTake()
    }

    private fun finishTake() {
        finishing = true
        upd { it.copy(busy = "Cleaning up your take…") }
        val r = JSONObject(NativeEngine.finalizeTake())
        finishing = false
        upd { it.copy(busy = null) }
        if (r.optBoolean("ok")) {
            val red = r.optDouble("reduction_db", 0.0)
            say(
                if (r.optBoolean("cleaned")) "Take saved. Speaker bleed reduced (${"%.1f".format(Locale.US, red)} dB overall)."
                else "Take saved (bleed cancellation off)."
            )
        } else say("Take failed: ${r.optString("error")}")
    }

    // -------------------------------------------------------------- tracks ---

    private fun setTrack(t: Int, n: TrackUi) {
        upd { s -> s.copy(tracks = s.tracks.toMutableList().also { l -> l[t] = n }) }
        NativeEngine.setTrack(t, n.gain, n.mute, n.solo)
        prefs.saveTrack(t, n.gain, n.mute, n.solo)
    }

    fun setGain(t: Int, g: Float) = setTrack(t, ui.tracks[t].copy(gain = g))
    fun toggleMute(t: Int) = setTrack(t, ui.tracks[t].copy(mute = !ui.tracks[t].mute))
    fun toggleSolo(t: Int) = setTrack(t, ui.tracks[t].copy(solo = !ui.tracks[t].solo))

    fun clear(t: Int) {
        if (ui.state != 0) return
        viewModelScope.launch(Dispatchers.Default) { NativeEngine.clearTrack(t); say("Track ${t + 1} cleared.") }
    }

    fun reclean(t: Int) {
        if (ui.state != 0 || ui.busy != null) return
        viewModelScope.launch(Dispatchers.Default) {
            upd { it.copy(busy = "Re-processing track ${t + 1}…") }
            val r = JSONObject(NativeEngine.reclean(t))
            upd { it.copy(busy = null) }
            say(if (r.optBoolean("ok")) "Track ${t + 1} re-processed with current settings." else "Can't re-process: ${r.optString("error")}")
        }
    }

    fun clearAll() {
        if (ui.state != 0) return
        viewModelScope.launch(Dispatchers.Default) { NativeEngine.clearAll(); say("All tracks cleared.") }
    }

    fun exportMix() {
        if (ui.state != 0 || ui.busy != null) return
        viewModelScope.launch(Dispatchers.Default) {
            val ctx = getApplication<Application>()
            val f = File(ctx.cacheDir, "share/fortrakz-mix.wav").apply { parentFile?.mkdirs() }
            if (NativeEngine.exportMix(f.absolutePath)) Share.shareFile(ctx, f, "audio/wav", "Share mixdown")
            else say("Nothing to export yet.")
        }
    }

    // ------------------------------------------------------------ settings ---

    private fun restartEngine() {
        viewModelScope.launch(Dispatchers.Default) {
            endTransport()
            NativeEngine.stopStreams()
            applyConfig()
            startEngine()
        }
    }

    fun setPreset(p: Int) { prefs.inputPreset = p; settingsTick++; restartEngine(); markUncalibrated() }
    fun setPerf(p: Int) { prefs.perfMode = p; settingsTick++; restartEngine(); markUncalibrated() }
    fun setClean(m: Int) { prefs.cleanMode = m; settingsTick++; NativeEngine.setCleanMode(m) }
    fun setTrim(ms: Int) {
        prefs.trimMs = ms.coerceIn(-150, 150); settingsTick++
        val rtl = prefs.rtlFrames
        NativeEngine.setLatencyFrames(if (rtl >= 0) max(0, rtl + prefs.trimMs * 48) else 0)
    }

    private fun markUncalibrated() {
        upd { it.copy(calibrated = false) }
        say("Audio path changed: run calibrate again.")
    }

    fun calibrate() {
        if (ui.busy != null || lab.running || ui.state != 0) return
        viewModelScope.launch(Dispatchers.Default) {
            upd { it.copy(busy = "Calibrating… ~15 s. Quiet room, phone on a table, volume where you'll use it.") }
            val dir = File(labRoot, "calibration").apply { mkdirs() }
            val j = runCatching { JSONObject(NativeEngine.calibrate(dir.absolutePath)) }.getOrNull()
            applyConfig()
            if (j != null && j.optBoolean("ok")) {
                prefs.rtlFrames = j.getDouble("rtl_frames").roundToInt()
                val ms = prefs.rtlFrames / 48.0
                val echoRes = j.optJSONObject("echo")?.optJSONObject("result")
                val erle = echoRes?.optDouble("erle_test_db", Double.NaN) ?: Double.NaN
                prefs.calibNote =
                    "Round trip ${"%.1f".format(Locale.US, ms)} ms" +
                        if (j.optBoolean("echo_ok") && !erle.isNaN()) " · bleed cancel ${"%.0f".format(Locale.US, erle)} dB" else " · echo path failed (turn volume up)"
                NativeEngine.setLatencyFrames(max(0, prefs.rtlFrames + prefs.trimMs * 48))
            } else {
                prefs.calibNote = "Calibration failed: the mic didn't hear the chirps. Raise volume / check mic."
            }
            settingsTick++
            upd { it.copy(busy = null, calibrated = NativeEngine.echoValid() && prefs.rtlFrames >= 0) }
            startEngine()
            say(prefs.calibNote)
        }
    }

    // ----------------------------------------------------------------- lab ---

    private fun deviceJson(): JSONObject {
        val ctx = getApplication<Application>()
        val am = ctx.getSystemService(Context.AUDIO_SERVICE) as AudioManager
        val routes = am.getDevices(AudioManager.GET_DEVICES_ALL).joinToString(",") { d ->
            (if (d.isSink) "out:" else "in:") + d.type + ":" + (d.productName ?: "")
        }
        return JSONObject().apply {
            put("manufacturer", Build.MANUFACTURER); put("model", Build.MODEL); put("device", Build.DEVICE)
            put("android", Build.VERSION.RELEASE); put("sdk", Build.VERSION.SDK_INT)
            put("native_sample_rate", am.getProperty(AudioManager.PROPERTY_OUTPUT_SAMPLE_RATE))
            put("native_frames_per_buffer", am.getProperty(AudioManager.PROPERTY_OUTPUT_FRAMES_PER_BUFFER))
            put("unprocessed_supported", am.getProperty(AudioManager.PROPERTY_SUPPORT_AUDIO_SOURCE_UNPROCESSED))
            put("feature_low_latency", ctx.packageManager.hasSystemFeature(PackageManager.FEATURE_AUDIO_LOW_LATENCY))
            put("feature_pro", ctx.packageManager.hasSystemFeature(PackageManager.FEATURE_AUDIO_PRO))
            put("music_volume", am.getStreamVolume(AudioManager.STREAM_MUSIC))
            put("music_volume_max", am.getStreamMaxVolume(AudioManager.STREAM_MUSIC))
            put("audio_devices", routes)
            put("app_version", "0.1.0")
        }
    }

    private fun summarize(tag: String, latJson: String, echoJson: String?): LabRow {
        val lat = runCatching { JSONObject(latJson).getJSONObject("result") }.getOrNull()
        val echo = echoJson?.let { runCatching { JSONObject(it).getJSONObject("result") }.getOrNull() }
        val l1 = if (lat != null && lat.has("median_ms"))
            "RTL ${"%.1f".format(Locale.US, lat.getDouble("median_ms"))} ms  ±${"%.1f".format(Locale.US, lat.getDouble("std_frames") / 48.0)} ms  (${lat.getInt("valid")}/${lat.getInt("sent")} heard)"
        else "latency: not heard (${lat?.optInt("valid", 0) ?: 0}/${lat?.optInt("sent", 0) ?: 0})"
        val l2 = when {
            echo == null -> ""
            echo.optBoolean("ok") -> "bleed cancel ${"%.0f".format(Locale.US, echo.getDouble("erle_test_db"))} dB  (adaptive ${"%.0f".format(Locale.US, echo.getDouble("erle_test_adaptive_db"))} dB)"
            else -> "echo: ${echo.optString("error", "failed")}"
        }
        return LabRow(tag, l1, l2, lat?.has("median_ms") == true && (echo == null || echo.optBoolean("ok")))
    }

    /** kind: "quick" current settings, "matrix" every preset x perf mode, "jitter" 6 restarts of latency only. */
    fun runLab(kind: String) {
        if (lab.running || ui.busy != null || ui.state != 0) return
        viewModelScope.launch(Dispatchers.Default) {
            val stamp = SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US).format(Date())
            val dir = File(labRoot, "run-$stamp-$kind").apply { mkdirs() }
            lab = LabUi(running = true, progress = "Starting…", lastDir = dir.absolutePath)
            NativeEngine.stopStreams()
            val combos: List<Pair<Int, Int>> = when (kind) {
                "matrix" -> INPUT_PRESETS.flatMap { p -> listOf(12, 10).map { m -> p.first to m } }
                else -> listOf(prefs.inputPreset to prefs.perfMode)
            }
            val runs = ArrayList<String>()
            val rows = ArrayList<LabRow>()
            val reps = if (kind == "jitter") 6 else 1
            var n = 0
            val total = combos.size * reps
            for ((preset, perf) in combos) for (rep in 0 until reps) {
                n++
                val tag = "p${preset}_m${perf}" + if (reps > 1) "_r$rep" else ""
                lab = lab.copy(progress = "[$n/$total] $tag … keep the room quiet")
                val lat = NativeEngine.labLatency(preset, perf, dir.absolutePath, tag)
                val echo = if (kind == "jitter") null else NativeEngine.labEcho(preset, perf, dir.absolutePath, tag)
                runs.add("{\"tag\":\"$tag\",\"latency\":$lat" + (if (echo != null) ",\"echo\":$echo" else "") + "}")
                rows.add(summarize(tag, lat, echo))
                lab = lab.copy(rows = rows.toList())
            }
            val json = "{\"kind\":\"$kind\",\"timestamp\":\"$stamp\",\"device\":${deviceJson()},\"runs\":[${runs.joinToString(",")}]}"
            File(dir, "results.json").writeText(json)
            applyConfig()
            startEngine()
            lab = lab.copy(running = false, progress = "Done. ${rows.size} run(s) saved. Tap Share to send me the zip.")
        }
    }

    fun shareLab() {
        val d = lab.lastDir?.let { File(it) } ?: return
        viewModelScope.launch(Dispatchers.Default) {
            val ctx = getApplication<Application>()
            val z = File(ctx.cacheDir, "share/fortrakz-lab-${d.name}.zip")
            Share.zipDir(d, z)
            Share.shareFile(ctx, z, "application/zip", "Share lab results")
        }
    }
}
