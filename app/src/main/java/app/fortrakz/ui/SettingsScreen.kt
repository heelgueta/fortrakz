package app.fortrakz.ui

import androidx.compose.foundation.background
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
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import app.fortrakz.INPUT_PRESETS
import app.fortrakz.StudioVM
import java.util.Locale

@Composable
fun Header(title: String, onBack: () -> Unit) {
    Row(Modifier.fillMaxWidth().padding(bottom = 10.dp), verticalAlignment = Alignment.CenterVertically) {
        IconSquare(52.dp, C.surface, C.text, iconBack, onClick = onBack)
        Spacer(Modifier.size(12.dp))
        Text(title, color = C.text, fontSize = 24.sp, fontWeight = FontWeight.Bold)
    }
}

@Composable
fun BigButton(text: String, color: androidx.compose.ui.graphics.Color, enabled: Boolean = true, onClick: () -> Unit) {
    Box(
        Modifier.fillMaxWidth().height(64.dp).clip(RoundedCornerShape(16.dp))
            .background(if (enabled) color else C.surface2).clickable(enabled = enabled, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) { Text(text, color = if (enabled) C.bg else C.dim, fontSize = 20.sp, fontWeight = FontWeight.Bold) }
}

@Composable
fun Card(content: @Composable () -> Unit) {
    Column(Modifier.fillMaxWidth().clip(RoundedCornerShape(16.dp)).background(C.surface).padding(14.dp)) { content() }
}

@Composable
fun Choice(label: String, selected: Boolean, onClick: () -> Unit) {
    Row(
        Modifier.fillMaxWidth().clip(RoundedCornerShape(10.dp)).background(if (selected) C.surface2 else C.surface)
            .clickable(onClick = onClick).padding(12.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(Modifier.size(18.dp).clip(RoundedCornerShape(9.dp)).background(if (selected) C.green else C.surface2))
        Spacer(Modifier.size(12.dp))
        Text(label, color = C.text, fontSize = 16.sp)
    }
}

@Composable
fun SettingsScreen(vm: StudioVM, onBack: () -> Unit) {
    @Suppress("UNUSED_VARIABLE") val tick = vm.settingsTick  // re-read prefs after changes
    val p = vm.prefs
    Column(Modifier.fillMaxSize().background(C.bg).padding(14.dp).verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Header("Setup", onBack)

        Card {
            Text("Calibrate", color = C.text, fontSize = 18.sp, fontWeight = FontWeight.Bold)
            Spacer(Modifier.height(4.dp))
            Text(
                "Plays chirps then noise through the speaker and listens with the mic. Measures how late your recordings arrive and learns how the speaker leaks into the mic so it can be subtracted. Quiet room, phone on a table, volume where you'll sing.",
                color = C.dim, fontSize = 14.sp,
            )
            Spacer(Modifier.height(10.dp))
            BigButton("CALIBRATE", C.green, enabled = vm.ui.state == 0 && vm.ui.busy == null) { vm.calibrate() }
            if (p.calibNote.isNotEmpty()) {
                Spacer(Modifier.height(8.dp))
                Text(p.calibNote, color = C.text, fontSize = 15.sp)
            }
        }

        Card {
            Text("Bleed cancellation", color = C.text, fontSize = 18.sp, fontWeight = FontWeight.Bold)
            Spacer(Modifier.height(6.dp))
            Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                Choice("Off (raw mic)", p.cleanMode == 0) { vm.setClean(0) }
                Choice("Subtract speaker (calibrated)", p.cleanMode == 1) { vm.setClean(1) }
                Choice("Subtract + track changes (slower)", p.cleanMode == 2) { vm.setClean(2) }
            }
            Spacer(Modifier.height(4.dp))
            Text("Applies to new takes. Use the refresh icon on a track to re-process it.", color = C.dim, fontSize = 13.sp)
        }

        Card {
            Text("Timing trim", color = C.text, fontSize = 18.sp, fontWeight = FontWeight.Bold)
            Text(
                "Fine-tune if new takes still feel early (−) or late (+). Re-process a track after changing it.",
                color = C.dim, fontSize = 13.sp,
            )
            Row(verticalAlignment = Alignment.CenterVertically) {
                IconSquare(48.dp, C.surface2, C.text, { c -> drawLine(c, androidx.compose.ui.geometry.Offset(size.width * .3f, size.height * .5f), androidx.compose.ui.geometry.Offset(size.width * .7f, size.height * .5f), size.width * .08f) }) { vm.setTrim(p.trimMs - 1) }
                Text(
                    String.format(Locale.US, "%+d ms", p.trimMs), color = C.text, fontSize = 22.sp,
                    fontWeight = FontWeight.Bold, modifier = Modifier.weight(1f), textAlign = androidx.compose.ui.text.style.TextAlign.Center,
                )
                IconSquare(48.dp, C.surface2, C.text, { c ->
                    drawLine(c, androidx.compose.ui.geometry.Offset(size.width * .3f, size.height * .5f), androidx.compose.ui.geometry.Offset(size.width * .7f, size.height * .5f), size.width * .08f)
                    drawLine(c, androidx.compose.ui.geometry.Offset(size.width * .5f, size.height * .3f), androidx.compose.ui.geometry.Offset(size.width * .5f, size.height * .7f), size.width * .08f)
                }) { vm.setTrim(p.trimMs + 1) }
            }
            Slider(
                value = p.trimMs.toFloat(), onValueChange = { vm.setTrim(it.toInt()) }, valueRange = -100f..100f,
                colors = SliderDefaults.colors(thumbColor = C.text, activeTrackColor = C.blue, inactiveTrackColor = C.surface2),
            )
        }

        Card {
            Text("Microphone mode", color = C.text, fontSize = 18.sp, fontWeight = FontWeight.Bold)
            Text("Changing this means calibrating again. The lab compares them all for you.", color = C.dim, fontSize = 13.sp)
            Spacer(Modifier.height(6.dp))
            Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                INPUT_PRESETS.forEach { (id, name) -> Choice(name, p.inputPreset == id) { vm.setPreset(id) } }
            }
        }

        Card {
            Text("Latency mode", color = C.text, fontSize = 18.sp, fontWeight = FontWeight.Bold)
            Spacer(Modifier.height(6.dp))
            Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                Choice("Low latency (recommended)", p.perfMode == 12) { vm.setPerf(12) }
                Choice("Default", p.perfMode == 10) { vm.setPerf(10) }
            }
        }

        Card {
            Text("Project", color = C.text, fontSize = 18.sp, fontWeight = FontWeight.Bold)
            Spacer(Modifier.height(8.dp))
            BigButton("CLEAR ALL TRACKS", C.red, enabled = vm.ui.state == 0 && vm.ui.busy == null) { vm.clearAll() }
        }
        Spacer(Modifier.height(24.dp))
    }
}
