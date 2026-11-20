package app.fortrakz.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import app.fortrakz.StudioVM
import app.fortrakz.TrackUi
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import java.util.Locale
import kotlin.math.ceil
import kotlin.math.max
import kotlin.math.min
import kotlin.math.sqrt

private val laneColors = listOf(C.blue, C.green, C.amber, Color(0xFFC78BFF))

private fun clock(sec: Float): String {
    val t = (sec * 10).toInt().coerceAtLeast(0)
    return String.format(Locale.US, "%d:%02d.%d", t / 600, (t / 10) % 60, t % 10)
}

@Composable
fun StudioScreen(vm: StudioVM, onSettings: () -> Unit, onLab: () -> Unit) {
    val ui = vm.ui
    val recording = ui.state == 2 || (ui.state == 3 && ui.startingRecord)
    val playing = ui.state == 1 || (ui.state == 3 && !ui.startingRecord)
    val counting = ui.state == 3
    // every lane shows the same time window so playheads line up; grows in 10 s steps
    val shown = max(ui.len, ui.pos + (if (ui.state == 2) ui.takeSec else 0f))
    val windowSec = max(20f, ceil((shown * 1.05f + 2f) / 10f) * 10f)
    val recStart = ui.pos - ui.takeSec

    Box(Modifier.fillMaxSize().background(C.bg)) {
        Column(Modifier.fillMaxSize().padding(horizontal = 12.dp, vertical = 8.dp)) {
            // ---- header
            Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                Column(Modifier.weight(1f)) {
                    Text("FORTRAKZ", color = C.dim, fontSize = 12.sp, fontWeight = FontWeight.Bold, letterSpacing = 3.sp)
                    Text(
                        if (counting) "COUNT ${ui.countBeat}" else clock(if (ui.state == 2) ui.pos else ui.pos),
                        color = if (recording) C.red else if (counting) C.amber else C.text, fontSize = 36.sp,
                        fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Bold,
                    )
                }
                IconSquare(48.dp, C.surface, C.text, iconShare, enabled = ui.state == 0 && ui.busy == null) { vm.exportMix() }
                Spacer(Modifier.size(8.dp))
                IconSquare(48.dp, C.surface, C.text, iconFlask) { onLab() }
                Spacer(Modifier.size(8.dp))
                IconSquare(48.dp, C.surface, C.text, iconSliders) { onSettings() }
            }
            Spacer(Modifier.height(6.dp))
            ClickRow(vm)
            Spacer(Modifier.height(6.dp))

            if (!ui.calibrated) {
                Box(
                    Modifier.fillMaxWidth().clip(RoundedCornerShape(12.dp)).background(C.amber.copy(alpha = .16f))
                        .border(1.dp, C.amber, RoundedCornerShape(12.dp)).clickable { onSettings() }.padding(10.dp),
                ) { Text("Not calibrated. Tap to calibrate so overdubs line up and bleed is cancelled.", color = C.amber, fontSize = 13.sp) }
                Spacer(Modifier.height(6.dp))
            }

            // ---- tracks
            Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                for (t in 0 until 4) {
                    Box(Modifier.weight(1f)) {
                        TrackStrip(
                            vm = vm, index = t, tr = ui.tracks[t], armed = ui.armed == t,
                            recording = recording && ui.armed == t, locked = ui.state != 0 || ui.busy != null,
                            windowSec = windowSec, recStart = recStart,
                        )
                    }
                }
            }

            Spacer(Modifier.height(6.dp))
            ui.message?.let {
                Box(Modifier.fillMaxWidth().clip(RoundedCornerShape(10.dp)).background(C.surface2).padding(10.dp)) {
                    Text(it, color = C.text, fontSize = 13.sp)
                }
                Spacer(Modifier.height(6.dp))
            }

            // ---- transport
            Row(
                Modifier.fillMaxWidth().padding(bottom = 4.dp),
                horizontalArrangement = Arrangement.SpaceEvenly, verticalAlignment = Alignment.CenterVertically,
            ) {
                RoundButton(62.dp, C.surface, C.text, iconRewind, enabled = ui.state < 2) { vm.rewind() }
                RoundButton(70.dp, C.surface2, C.text, iconStop) { vm.stop() }
                RoundButton(84.dp, if (playing) C.green else C.surface2, if (playing) C.bg else C.green, iconPlay, enabled = ui.state < 2) { vm.play() }
                RoundButton(84.dp, if (recording) C.red else C.surface2, if (recording) C.text else C.red, iconRec, enabled = ui.state < 2) { vm.record() }
            }
        }

        ui.busy?.let {
            Box(Modifier.fillMaxSize().background(Color(0xCC000000)), contentAlignment = Alignment.Center) {
                Box(Modifier.padding(32.dp).clip(RoundedCornerShape(16.dp)).background(C.surface).padding(24.dp)) {
                    Text(it, color = C.text, fontSize = 18.sp)
                }
            }
        }
    }
}

/** Metronome controls: bpm, count-in beats (0 = off), click while playing/recording. */
@Composable
private fun ClickRow(vm: StudioVM) {
    val ui = vm.ui
    Row(
        Modifier.fillMaxWidth().clip(RoundedCornerShape(14.dp)).background(C.surface).padding(horizontal = 8.dp, vertical = 6.dp),
        verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.SpaceBetween,
    ) {
        Stepper("BPM", ui.bpm.toString(), { vm.setBpm(ui.bpm + it) })
        Stepper("COUNT-IN", if (ui.countTotal == 0) "off" else ui.countTotal.toString(), { vm.setCountIn(ui.countTotal + it) })
        Column(horizontalAlignment = Alignment.CenterHorizontally) {
            Text("CLICK", color = C.dim, fontSize = 10.sp, fontWeight = FontWeight.Bold, letterSpacing = 1.sp)
            SquareButton(44.dp, if (ui.clickPlay) C.amber else C.surface2, onClick = { vm.toggleClickPlay() }) {
                Text(if (ui.clickPlay) "ON" else "off", color = if (ui.clickPlay) C.bg else C.dim, fontWeight = FontWeight.Bold, fontSize = 14.sp)
            }
        }
    }
}

@Composable
private fun Stepper(label: String, value: String, change: (Int) -> Unit) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        HoldButton("−") { change(-1) }
        Column(Modifier.width(66.dp), horizontalAlignment = Alignment.CenterHorizontally) {
            Text(label, color = C.dim, fontSize = 10.sp, fontWeight = FontWeight.Bold, letterSpacing = 1.sp)
            Text(value, color = C.text, fontSize = 22.sp, fontWeight = FontWeight.Bold, fontFamily = FontFamily.Monospace)
        }
        HoldButton("+") { change(1) }
    }
}

/** Tap to step once; hold to repeat (bpm 30..300 would be tedious otherwise). */
@Composable
private fun HoldButton(text: String, step: () -> Unit) {
    val scope = rememberCoroutineScope()
    Box(
        Modifier.size(44.dp).clip(RoundedCornerShape(10.dp)).background(C.surface2).pointerInput(Unit) {
            detectTapGestures(onPress = {
                val job = scope.launch { step(); delay(380); while (true) { step(); delay(70) } }
                tryAwaitRelease()
                job.cancel()
            })
        },
        contentAlignment = Alignment.Center,
    ) { Text(text, color = C.text, fontSize = 24.sp, fontWeight = FontWeight.Bold) }
}

@Composable
private fun TrackStrip(
    vm: StudioVM, index: Int, tr: TrackUi, armed: Boolean, recording: Boolean, locked: Boolean,
    windowSec: Float, recStart: Float,
) {
    val shape = RoundedCornerShape(16.dp)
    val col = laneColors[index]
    Row(
        Modifier.fillMaxSize().clip(shape).background(C.surface)
            .border(if (armed) 2.dp else 0.dp, if (armed) C.red else Color.Transparent, shape).padding(8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(
            Modifier.size(56.dp).clip(CircleShape).background(if (recording) C.red else if (armed) C.red.copy(alpha = .35f) else C.surface2)
                .border(3.dp, C.red, CircleShape).clickable(enabled = !locked || recording) { vm.arm(index) },
            contentAlignment = Alignment.Center,
        ) { Text("${index + 1}", color = C.text, fontSize = 24.sp, fontWeight = FontWeight.Bold) }

        Spacer(Modifier.size(8.dp))
        Column(Modifier.weight(1f).fillMaxSize(), verticalArrangement = Arrangement.spacedBy(2.dp)) {
            Lane(vm, index, windowSec, recStart, col, recording, Modifier.weight(1f).fillMaxWidth())
            Slider(
                value = tr.gain, onValueChange = { vm.setGain(index, it) }, valueRange = 0f..3f,
                colors = SliderDefaults.colors(thumbColor = C.text, activeTrackColor = col, inactiveTrackColor = C.surface2),
                modifier = Modifier.fillMaxWidth().height(30.dp),
            )
        }
        Spacer(Modifier.size(8.dp))
        Column(verticalArrangement = Arrangement.spacedBy(5.dp), horizontalAlignment = Alignment.CenterHorizontally) {
            Row(horizontalArrangement = Arrangement.spacedBy(5.dp)) {
                SquareButton(40.dp, if (tr.mute) C.amber else C.surface2, onClick = { vm.toggleMute(index) }) {
                    Text("M", color = if (tr.mute) C.bg else C.text, fontWeight = FontWeight.Bold, fontSize = 17.sp)
                }
                SquareButton(40.dp, if (tr.solo) C.green else C.surface2, onClick = { vm.toggleSolo(index) }) {
                    Text("S", color = if (tr.solo) C.bg else C.text, fontWeight = FontWeight.Bold, fontSize = 17.sp)
                }
            }
            Row(horizontalArrangement = Arrangement.spacedBy(5.dp)) {
                IconSquare(40.dp, C.surface2, C.text, iconRefresh, enabled = tr.hasTake && !locked) { vm.reclean(index) }
                IconSquare(40.dp, C.surface2, C.text, iconTrash, enabled = tr.hasTake && !locked) { vm.clear(index) }
            }
        }
    }
}

/**
 * One waveform lane. Every lane shares the same time axis, so the white playhead is at the same
 * x on all four tracks; faint lines mark beats (brighter every 4th = bar). Tap or drag to seek.
 */
@Composable
private fun Lane(vm: StudioVM, t: Int, windowSec: Float, recStart: Float, color: Color, recording: Boolean, modifier: Modifier) {
    val ui = vm.ui
    @Suppress("UNUSED_VARIABLE") val ver = ui.waveVer  // redraw when waveforms change
    val pos = ui.pos
    val liveN = ui.liveN
    val bpm = ui.bpm
    val shiftSec = max(0, vm.prefs.rtlFrames) / 48000f  // live trace is shifted like the finished take will be
    Canvas(
        modifier.clip(RoundedCornerShape(8.dp)).background(Color(0xFF14161A))
            .pointerInput(windowSec) { detectTapGestures { o -> vm.seek(o.x / size.width * windowSec) } }
            .pointerInput(windowSec) { detectDragGestures { c, _ -> c.consume(); vm.seek(c.position.x.coerceIn(0f, size.width.toFloat()) / size.width * windowSec) } },
    ) {
        val w = size.width
        val h = size.height
        // beat / bar grid
        val beatSec = 60f / bpm
        val pxBeat = w * beatSec / windowSec
        var b = 0
        while (b * beatSec <= windowSec) {
            val bar = b % 4 == 0
            if (pxBeat >= 6f || (bar && pxBeat * 4 >= 6f)) {
                val x = w * b * beatSec / windowSec
                drawLine(Color.White.copy(alpha = if (bar) 0.16f else 0.06f), Offset(x, 0f), Offset(x, h), 1f)
            }
            b++
        }
        // finished audio
        val data = vm.waves[t]
        val bucket = 0.05f
        if (data.isNotEmpty()) {
            val cols = w.toInt()
            for (x in 0 until cols) {
                val i0 = ((x / w) * windowSec / bucket).toInt()
                if (i0 >= data.size) break
                val i1 = max(i0 + 1, (((x + 1) / w) * windowSec / bucket).toInt())
                var m = 0f
                for (i in i0 until min(i1, data.size)) m = max(m, data[i])
                val a = min(1f, sqrt(m)) * h * 0.46f  // sqrt so quiet takes are still visible
                if (a > 0.4f) drawLine(color, Offset(x + .5f, h / 2 - a), Offset(x + .5f, h / 2 + a), 1f)
            }
        }
        // live trace while this lane is recording
        if (recording && liveN > 0) {
            val live = vm.live
            for (i in 0 until liveN) {
                val x = w * ((recStart - 0f + i * bucket - shiftSec) / windowSec)
                if (x < 0f || x > w) continue
                val a = min(1f, sqrt(live[i])) * h * 0.46f
                drawLine(C.red, Offset(x, h / 2 - a), Offset(x, h / 2 + a), 2f)
            }
        }
        // playhead
        val px = (pos / windowSec * w).coerceIn(0f, w)
        drawLine(Color.White, Offset(px, 0f), Offset(px, h), 2f)
    }
}
