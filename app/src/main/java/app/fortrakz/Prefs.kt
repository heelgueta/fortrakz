package app.fortrakz

import android.content.Context

class Prefs(ctx: Context) {
    private val sp = ctx.getSharedPreferences("fortrakz", Context.MODE_PRIVATE)

    var inputPreset: Int
        get() = sp.getInt("preset", 9)
        set(v) = sp.edit().putInt("preset", v).apply()
    var perfMode: Int
        get() = sp.getInt("perf", 12)
        set(v) = sp.edit().putInt("perf", v).apply()
    /** 0 off, 1 fixed echo path, 2 fixed + adaptive tracking */
    var cleanMode: Int
        get() = sp.getInt("clean", 1)
        set(v) = sp.edit().putInt("clean", v).apply()
    var trimMs: Int
        get() = sp.getInt("trim", 0)
        set(v) = sp.edit().putInt("trim", v).apply()
    /** Measured round trip in frames at 48 kHz, -1 when never calibrated. */
    var rtlFrames: Int
        get() = sp.getInt("rtl", -1)
        set(v) = sp.edit().putInt("rtl", v).apply()
    var calibNote: String
        get() = sp.getString("calibNote", "") ?: ""
        set(v) = sp.edit().putString("calibNote", v).apply()

    fun gain(t: Int) = sp.getFloat("gain$t", 0.8f)
    fun mute(t: Int) = sp.getBoolean("mute$t", false)
    fun solo(t: Int) = sp.getBoolean("solo$t", false)
    fun saveTrack(t: Int, gain: Float, mute: Boolean, solo: Boolean) =
        sp.edit().putFloat("gain$t", gain).putBoolean("mute$t", mute).putBoolean("solo$t", solo).apply()
}
