# Fortrakz

A 4-track tape-style recorder for Android that works with nothing but the phone's own mic and speaker.
Record track 1, then sing along while it plays and record track 2 on top, and so on. Every new take is
(1) shifted back by the measured round-trip latency so it lines up, and (2) cleaned of the backing
track that leaked from the speaker into the mic.

No account, no network, no ads. Native C++ (Oboe/AAudio) engine, Kotlin + Compose UI, big buttons.

## Install
Download `fortrakz-0.2.0.apk` from the [latest release](https://github.com/heelgueta/fortrakz/releases/latest) (debug-signed release build). Copy to the phone, allow "install unknown apps", open.
`adb install -r fortrakz-0.2.0.apk` also works with USB debugging.

## Use
1. **Setup → Calibrate** once (quiet room, phone on a table, moderate volume). ~20 s. Calibrate again after changing route, volume or mic mode.
2. Set **BPM** and **COUNT-IN** (0 = off, 1-8 clicks) on the main screen. Tap the red numbered circle to arm a track and press ●. The count-in plays, then recording starts and the other tracks play.
3. ■ stops and processes the take (a second or two). ▶ plays back, ⏮ rewinds.
4. Each track has a **waveform lane**. All lanes share one time axis, so the white playhead shows where every track is; faint lines are beats (brighter = bar). Tap or drag a lane to jump there. A red trace shows the take being recorded.
5. M/S mute/solo, slider = volume, ↻ re-processes a take with current settings, 🗑 clears it. CLICK keeps the metronome going during play/record.
6. Share icon exports the mixdown as WAV.

## Is it even allowed? (yes)
Android lets an app play and record at once; nothing blocks it. The two real problems:
* **Latency**: speaker → mic path is typically tens of ms. Fixed by measuring it (chirps, cross-correlation) and
  placing each take at `position + captured_index − round_trip`.
* **Bleed**: the backing leaks into the mic. The app knows exactly what it played, so it subtracts
  `h * played_signal` from each take. The hard part is `h` (how the speaker sounds *in the mic*). Default mode
  **learns `h` and the exact delay from the take itself**, using the music that was playing at its real volume
  (voice is uncorrelated with it; blocks where you're singing loudly are down-weighted). Calibration noise is only
  the starting point. "Re-learn every 15 s" follows slow drift on long takes.
* **Stream sync**: input and output are tied by absolute frame counters (`native/core/aligner.h`); nothing is ever
  dropped or zero-filled, which on a real phone turned out to matter more than the filter (see `docs/LAB-NOTES.md`).

## The lab (Lab icon in the app)
* **Quick test** – latency (8 chirps) + echo path/ERLE with current settings.
* **Full matrix** – every mic mode (unprocessed / voice-recognition / camcorder / generic / voice-communication) × perf mode. Reports capture health (clipping, zero-filled frames, delay jumps).
* **Share last take** – raw mic + exactly what was playing for your last real recording, for offline tuning.
* **Restart jitter** – 6 stream restarts, to see how stable alignment is between sessions.

Each run saves `results.json` + WAV dumps (stimulus and capture). **Share results** zips them; run
`python3 tools/analyze.py <zip>` for a table, or share the zip with whoever is tuning the algorithms.

## Layout
```
native/core      pure C++17: FFT, DSP, echo path, Studio (mixer, takes, persistence), lab maths
native/android   Oboe duplex stream, lab runner, JNI
native/tests     host unit tests with simulated acoustics (no phone needed)
app/             Kotlin/Compose UI
tools/analyze.py lab result summary (stdlib only)
tools/labtool.cpp, blocktrack.cpp  offline diagnosis of lab captures (coherence ceiling, delay tracking)
docs/LAB-NOTES.md  what real-device data taught us
```

## Build / test
```
export JAVA_HOME=<jdk 17+> ANDROID_HOME=~/Android      # SDK 35, NDK 27.2.12479018, CMake 3.22.1
./gradlew assembleRelease                              # -> app/build/outputs/apk/release/
cmake -S native/tests -B /tmp/fz && cmake --build /tmp/fz && /tmp/fz/test_core
```

## Known limits (v0.2)
* Tested on one phone (Galaxy S23 Ultra). Engine logic is also unit-tested on the host against simulated acoustics.
* Calibrate again when you change route (headphones, Bluetooth), volume, or mic mode.
* Keep the app in the foreground while recording (Android restricts background mic use).
* 10 minutes per track. Mono, 48 kHz.
* Speaker non-linearity and hand-held movement limit how much bleed can be removed; this is what the lab measures.

## License
MIT (see `LICENSE`).
