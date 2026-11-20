package app.fortrakz.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
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
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import app.fortrakz.StudioVM
import app.fortrakz.TrackUi
import java.util.Locale

private fun clock(sec: Float): String {
    val t = (sec * 10).toInt().coerceAtLeast(0)
    return String.format(Locale.US, "%d:%02d.%d", t / 600, (t / 10) % 60, t % 10)
}

@Composable
fun StudioScreen(vm: StudioVM, onSettings: () -> Unit, onLab: () -> Unit) {
    val ui = vm.ui
    val recording = ui.state == 2
    val playing = ui.state == 1
    Box(Modifier.fillMaxSize().background(C.bg)) {
        Column(Modifier.fillMaxSize().padding(horizontal = 14.dp, vertical = 10.dp)) {
            // ---- header
            Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                Column(Modifier.weight(1f)) {
                    Text("FORTRAKZ", color = C.dim, fontSize = 13.sp, fontWeight = FontWeight.Bold, letterSpacing = 3.sp)
                    Text(
                        clock(if (recording) ui.pos + ui.takeSec else ui.pos),
                        color = if (recording) C.red else C.text, fontSize = 40.sp,
                        fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Bold,
                    )
                }
                IconSquare(52.dp, C.surface, C.text, iconShare, enabled = ui.state == 0 && ui.busy == null) { vm.exportMix() }
                Spacer(Modifier.size(8.dp))
                IconSquare(52.dp, C.surface, C.text, iconFlask) { onLab() }
                Spacer(Modifier.size(8.dp))
                IconSquare(52.dp, C.surface, C.text, iconSliders) { onSettings() }
            }
            Spacer(Modifier.height(8.dp))

            if (!ui.calibrated) {
                Box(
                    Modifier.fillMaxWidth().clip(RoundedCornerShape(12.dp)).background(C.amber.copy(alpha = .16f))
                        .border(1.dp, C.amber, RoundedCornerShape(12.dp)).clickable { onSettings() }.padding(12.dp),
                ) { Text("Not calibrated. Tap here to calibrate so overdubs line up and bleed is cancelled.", color = C.amber, fontSize = 14.sp) }
                Spacer(Modifier.height(8.dp))
            }

            // ---- tracks
            Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                for (t in 0 until 4) {
                    Box(Modifier.weight(1f)) {
                        TrackStrip(
                            index = t, tr = ui.tracks[t], armed = ui.armed == t, recording = recording && ui.armed == t,
                            peak = ui.peaks[t], locked = ui.state != 0 || ui.busy != null,
                            onArm = { vm.arm(t) }, onGain = { vm.setGain(t, it) }, onMute = { vm.toggleMute(t) },
                            onSolo = { vm.toggleSolo(t) }, onClear = { vm.clear(t) }, onReclean = { vm.reclean(t) },
                        )
                    }
                }
            }

            Spacer(Modifier.height(8.dp))
            ui.message?.let {
                Box(Modifier.fillMaxWidth().clip(RoundedCornerShape(10.dp)).background(C.surface2).padding(10.dp)) {
                    Text(it, color = C.text, fontSize = 14.sp)
                }
                Spacer(Modifier.height(8.dp))
            }

            // ---- transport
            Row(
                Modifier.fillMaxWidth().padding(bottom = 6.dp),
                horizontalArrangement = Arrangement.SpaceEvenly, verticalAlignment = Alignment.CenterVertically,
            ) {
                RoundButton(68.dp, C.surface, C.text, iconRewind, enabled = !recording) { vm.rewind() }
                RoundButton(76.dp, C.surface2, C.text, iconStop) { vm.stop() }
                RoundButton(92.dp, if (playing) C.green else C.surface2, if (playing) C.bg else C.green, iconPlay, enabled = !recording) { vm.play() }
                RoundButton(92.dp, if (recording) C.red else C.surface2, if (recording) C.text else C.red, iconRec, enabled = !recording) { vm.record() }
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

@Composable
private fun TrackStrip(
    index: Int, tr: TrackUi, armed: Boolean, recording: Boolean, peak: Float, locked: Boolean,
    onArm: () -> Unit, onGain: (Float) -> Unit, onMute: () -> Unit, onSolo: () -> Unit,
    onClear: () -> Unit, onReclean: () -> Unit,
) {
    val shape = RoundedCornerShape(16.dp)
    Row(
        Modifier.fillMaxSize().clip(shape).background(C.surface)
            .border(if (armed) 2.dp else 0.dp, if (armed) C.red else Color.Transparent, shape).padding(10.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        // big arm button
        Box(
            Modifier.size(64.dp).clip(CircleShape).background(if (recording) C.red else if (armed) C.red.copy(alpha = .35f) else C.surface2)
                .border(3.dp, C.red, CircleShape).clickable(enabled = !locked || recording, onClick = onArm),
            contentAlignment = Alignment.Center,
        ) { Text("${index + 1}", color = C.text, fontSize = 26.sp, fontWeight = FontWeight.Bold) }

        Spacer(Modifier.size(10.dp))
        Column(Modifier.weight(1f), verticalArrangement = Arrangement.Center) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(
                    if (armed) "ARMED" else if (tr.hasTake) "has audio" else "empty",
                    color = if (armed) C.red else C.dim, fontSize = 12.sp, fontWeight = FontWeight.Bold, letterSpacing = 1.sp,
                )
            }
            Meter(peak, Modifier.fillMaxWidth().height(12.dp))
            Slider(
                value = tr.gain, onValueChange = onGain, valueRange = 0f..3f,
                colors = SliderDefaults.colors(thumbColor = C.text, activeTrackColor = C.blue, inactiveTrackColor = C.surface2),
                modifier = Modifier.fillMaxWidth().height(34.dp),
            )
        }
        Spacer(Modifier.size(8.dp))
        Column(verticalArrangement = Arrangement.spacedBy(6.dp), horizontalAlignment = Alignment.CenterHorizontally) {
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                SquareButton(44.dp, if (tr.mute) C.amber else C.surface2, onClick = onMute) {
                    Text("M", color = if (tr.mute) C.bg else C.text, fontWeight = FontWeight.Bold, fontSize = 18.sp)
                }
                SquareButton(44.dp, if (tr.solo) C.green else C.surface2, onClick = onSolo) {
                    Text("S", color = if (tr.solo) C.bg else C.text, fontWeight = FontWeight.Bold, fontSize = 18.sp)
                }
            }
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                IconSquare(44.dp, C.surface2, C.text, iconRefresh, enabled = tr.hasTake && !locked, onClick = onReclean)
                IconSquare(44.dp, C.surface2, C.text, iconTrash, enabled = tr.hasTake && !locked, onClick = onClear)
            }
        }
    }
}

@Composable
private fun Meter(level: Float, modifier: Modifier) {
    Canvas(modifier) {
        drawRoundRect(C.surface2, size = size, cornerRadius = CornerRadius(size.height / 2))
        val l = level.coerceIn(0f, 1f)
        val col = if (l > .95f) C.red else if (l > .7f) C.amber else C.green
        if (l > 0.003f) drawRoundRect(col, size = Size(size.width * l, size.height), cornerRadius = CornerRadius(size.height / 2))
    }
}
