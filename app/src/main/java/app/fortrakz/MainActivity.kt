package app.fortrakz

import android.Manifest
import android.content.pm.PackageManager
import android.os.Bundle
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.compose.runtime.mutableStateOf
import androidx.core.content.ContextCompat
import app.fortrakz.ui.FortrakzApp

class MainActivity : ComponentActivity() {
    private val vm: StudioVM by viewModels()
    private val granted = mutableStateOf(false)

    private val askMic = registerForActivityResult(ActivityResultContracts.RequestPermission()) { ok ->
        granted.value = ok
        if (ok) vm.boot()
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        granted.value = ContextCompat.checkSelfPermission(this, Manifest.permission.RECORD_AUDIO) ==
            PackageManager.PERMISSION_GRANTED
        if (granted.value) vm.boot() else askMic.launch(Manifest.permission.RECORD_AUDIO)
        setContent { FortrakzApp(vm, granted.value) { askMic.launch(Manifest.permission.RECORD_AUDIO) } }
    }

    override fun onStop() {
        super.onStop()
        vm.onBackground()
    }

    override fun onStart() {
        super.onStart()
        vm.onForeground()
    }
}
