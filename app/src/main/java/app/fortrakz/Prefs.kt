package app.fortrakz

import android.content.Context

class Prefs(ctx: Context) {
    private val sp = ctx.getSharedPreferences("fortrakz", Context.MODE_PRIVATE)

    var inputPreset: Int
        get() = sp.getInt("preset", 9)
        set(v) = sp.edit().putInt("preset", v).apply()
    /** 10 = default, 12 = low latency. Default mode: low latency returned a dead mic on some phones. */
    var perfMode: Int
        get() = sp.getInt("perf2", 10)
        set(v) = sp.edit().putInt("perf2", v).apply()
    /** 0 off, 1 calibrated filter, 3 learn from each take, 4 learn + re-learn every 15 s */
    var cleanMode: Int
        get() = sp.getInt("clean2", 3)
        set(v) = sp.edit().putInt("clean2", v).apply()
    var bpm: Int
        get() = sp.getInt("bpm", 100)
        set(v) = sp.edit().putInt("bpm", v.coerceIn(30, 300)).apply()
    /** beats of click before recording starts (0 = off) */
    var countIn: Int
        get() = sp.getInt("countIn", 4)
        set(v) = sp.edit().putInt("countIn", v.coerceIn(0, 8)).apply()
    var clickPlay: Boolean
        get() = sp.getBoolean("clickPlay", false)
        set(v) = sp.edit().putBoolean("clickPlay", v).apply()
    var countOnPlay: Boolean
        get() = sp.getBoolean("countOnPlay", false)
        set(v) = sp.edit().putBoolean("countOnPlay", v).apply()
    var lastTakeTrack: Int
        get() = sp.getInt("lastTrack", -1)
        set(v) = sp.edit().putInt("lastTrack", v).apply()
    var trimMs: Int
        get() = sp.getInt("trim", 0)
        set(v) = sp.edit().putInt("trim", v).apply()
    /** Measured round trip in frames at 48 kHz, -1 when never calibrated. */
    var rtlFrames: Int
        get() = sp.getInt("rtl2", -1)
        set(v) = sp.edit().putInt("rtl2", v).apply()
    var calibNote: String
        get() = sp.getString("calibNote", "") ?: ""
        set(v) = sp.edit().putString("calibNote", v).apply()

    fun gain(t: Int) = sp.getFloat("gain$t", 0.8f)
    fun mute(t: Int) = sp.getBoolean("mute$t", false)
    fun solo(t: Int) = sp.getBoolean("solo$t", false)
    fun saveTrack(t: Int, gain: Float, mute: Boolean, solo: Boolean) =
        sp.edit().putFloat("gain$t", gain).putBoolean("mute$t", mute).putBoolean("solo$t", solo).apply()
}
