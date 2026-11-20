package app.fortrakz.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import app.fortrakz.StudioVM

@Composable
fun LabScreen(vm: StudioVM, onBack: () -> Unit) {
    val lab = vm.lab
    val idle = !lab.running && vm.ui.busy == null && vm.ui.state == 0
    Column(Modifier.fillMaxSize().background(C.bg).padding(14.dp).verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Header("Lab", onBack)
        Card {
            Text(
                "These tests play sounds through the speaker and record them with the mic, then measure round-trip latency and how well the speaker bleed can be subtracted. Quiet room, phone flat on a table, music volume where you'd really use it. Don't touch the phone while a test runs.",
                color = C.dim, fontSize = 14.sp,
            )
        }
        BigButton("QUICK TEST (current settings, ~15 s)", C.green, idle) { vm.runLab("quick") }
        BigButton("FULL MATRIX (all mic modes, ~4 min)", C.blue, idle) { vm.runLab("matrix") }
        BigButton("RESTART JITTER (6 restarts, ~45 s)", C.amber, idle) { vm.runLab("jitter") }
        if (lab.progress.isNotEmpty()) Text(lab.progress, color = C.text, fontSize = 16.sp, fontWeight = FontWeight.Bold)
        lab.rows.forEach { r ->
            Card {
                Text(r.tag, color = if (r.good) C.green else C.amber, fontSize = 14.sp, fontWeight = FontWeight.Bold)
                Text(r.line1, color = C.text, fontSize = 15.sp)
                if (r.line2.isNotEmpty()) Text(r.line2, color = C.text, fontSize = 15.sp)
            }
        }
        if (lab.lastDir != null && !lab.running && lab.rows.isNotEmpty()) {
            BigButton("SHARE RESULTS (zip)", C.red) { vm.shareLab() }
            Text(lab.lastDir, color = C.dim, fontSize = 12.sp)
        }
        Spacer(Modifier.height(24.dp))
    }
}
