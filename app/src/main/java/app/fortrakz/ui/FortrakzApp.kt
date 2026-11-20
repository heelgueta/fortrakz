package app.fortrakz.ui

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import app.fortrakz.StudioVM

private enum class Screen { Studio, Settings, Lab }

@Composable
fun FortrakzApp(vm: StudioVM, micGranted: Boolean, askMic: () -> Unit) {
    FortrakzTheme {
        var screen by remember { mutableStateOf(Screen.Studio) }
        if (!micGranted) {
            Column(
                Modifier.fillMaxSize().background(C.bg).padding(28.dp),
                verticalArrangement = Arrangement.Center, horizontalAlignment = Alignment.CenterHorizontally,
            ) {
                Text("Fortrakz needs the microphone to record.", color = C.text, fontSize = 20.sp)
                androidx.compose.foundation.layout.Spacer(Modifier.padding(12.dp))
                BigButton("ALLOW MICROPHONE", C.green) { askMic() }
            }
            return@FortrakzTheme
        }
        BackHandler(enabled = screen != Screen.Studio) { screen = Screen.Studio }
        when (screen) {
            Screen.Studio -> StudioScreen(vm, { screen = Screen.Settings }, { screen = Screen.Lab })
            Screen.Settings -> SettingsScreen(vm) { screen = Screen.Studio }
            Screen.Lab -> LabScreen(vm) { screen = Screen.Studio }
        }
        // calibration overlay also needs to show on the settings screen
        if (screen != Screen.Studio) vm.ui.busy?.let { BusyOverlay(it) }
    }
}

@Composable
private fun BusyOverlay(text: String) {
    androidx.compose.foundation.layout.Box(
        Modifier.fillMaxSize().background(androidx.compose.ui.graphics.Color(0xCC000000)),
        contentAlignment = Alignment.Center,
    ) {
        androidx.compose.foundation.layout.Box(
            Modifier.padding(32.dp).background(C.surface, androidx.compose.foundation.shape.RoundedCornerShape(16.dp)).padding(24.dp),
        ) { Text(text, color = C.text, fontSize = 18.sp) }
    }
}
