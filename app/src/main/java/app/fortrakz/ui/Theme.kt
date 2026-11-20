package app.fortrakz.ui

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

object C {
    val bg = Color(0xFF101114)
    val surface = Color(0xFF1B1D22)
    val surface2 = Color(0xFF262932)
    val text = Color(0xFFECEFF4)
    val dim = Color(0xFF8E94A3)
    val red = Color(0xFFFF4D4D)
    val green = Color(0xFF3DDC84)
    val amber = Color(0xFFFFB020)
    val blue = Color(0xFF5AA9FF)
}

@Composable
fun FortrakzTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = darkColorScheme(
            primary = C.red, onPrimary = C.text, background = C.bg, surface = C.surface,
            onSurface = C.text, onBackground = C.text, secondary = C.green,
        ),
        content = content,
    )
}
