package app.fortrakz

/** JNI bridge to libfortrakz (Oboe duplex engine + DSP). */
object NativeEngine {
    init { System.loadLibrary("fortrakz") }

    external fun init(projectDir: String): Boolean
    external fun setConfig(inputPreset: Int, perfMode: Int)
    external fun startStreams(): Boolean
    external fun stopStreams()
    external fun streamInfo(): String

    external fun play()
    /** Returns true when a finished take is waiting for [finalizeTake]. */
    external fun stopTransport(): Boolean
    external fun record(track: Int): Boolean
    external fun seek(seconds: Float)

    external fun setTrack(track: Int, gain: Float, mute: Boolean, solo: Boolean)
    external fun setLatencyFrames(frames: Int)
    external fun setCleanMode(mode: Int)

    external fun finalizeTake(): String
    external fun reclean(track: Int): String
    external fun clearTrack(track: Int)
    external fun clearAll()
    external fun hasTake(track: Int): Boolean
    external fun echoValid(): Boolean
    external fun saveProject(): Boolean
    external fun exportMix(path: String): Boolean

    /** [pos s, len s, state, peak0..3, inPeak, takeSeconds, armed, xruns, streamsOpen] */
    external fun status(out: FloatArray)

    external fun calibrate(dumpDir: String): String
    external fun labLatency(preset: Int, perf: Int, dumpDir: String, tag: String): String
    external fun labEcho(preset: Int, perf: Int, dumpDir: String, tag: String): String
}
