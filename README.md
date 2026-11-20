# Fortrakz

A 4-track tape-style recorder for Android that works with nothing but the phone's own mic and speaker.
Record track 1, then sing along while it plays and record track 2 on top, and so on. Every new take is
(1) shifted back by the measured round-trip latency so it lines up, and (2) cleaned of the backing
track that leaked from the speaker into the mic.

No account, no network, no ads. Native C++ (Oboe/AAudio) engine, Kotlin + Compose UI, big buttons.

## Install
`dist/fortrakz-0.1.0.apk` (debug-signed release build). Copy to the phone, allow "install unknown apps", open.
`adb install -r dist/fortrakz-0.1.0.apk` also works with USB debugging.

## Use
1. **Setup → Calibrate** once (quiet room, phone on a table, volume where you'll sing). ~15 s.
2. Tap the red numbered circle on a track to arm it, press ● to record. Others play while you record.
3. ■ stops and processes the take (a second or two). ▶ plays back, ⏮ rewinds.
4. M/S mute/solo, slider = volume, ↻ re-processes a take with current settings, 🗑 clears it.
5. Share icon exports the mixdown as WAV.

## Is it even allowed? (yes)
Android lets an app play and record at once; nothing blocks it. The two real problems:
* **Latency**: speaker → mic path is typically tens of ms. Fixed by measuring it (chirps, cross-correlation) and
  placing each take at `position + captured_index − round_trip`.
* **Bleed**: the backing leaks into the mic. Fixed by calibrating the speaker→mic impulse response with white noise
  (cross-correlation + Richardson refinement), then subtracting `h * played_signal` from each take. An optional
  guarded NLMS stage tracks small path changes and freezes while you are singing.

## The lab (Lab icon in the app)
* **Quick test** – latency (8 chirps) + echo path/ERLE with current settings.
* **Full matrix** – every mic mode (unprocessed / voice-recognition / camcorder / generic / voice-communication) × perf mode.
* **Restart jitter** – 6 stream restarts, to see how stable alignment is between sessions.

Each run saves `results.json` + WAV dumps (stimulus and capture). **Share results** zips them; run
`python3 tools/analyze.py <zip>` for a table, or just send the zip to Claude to tune the algorithms.

## Layout
```
native/core      pure C++17: FFT, DSP, echo path, Studio (mixer, takes, persistence), lab maths
native/android   Oboe duplex stream, lab runner, JNI
native/tests     host unit tests with simulated acoustics (no phone needed)
app/             Kotlin/Compose UI
tools/analyze.py lab result summary (stdlib only)
```

## Build / test
```
export JAVA_HOME=<jdk 17+> ANDROID_HOME=~/Android      # SDK 35, NDK 27.2.12479018, CMake 3.22.1
./gradlew assembleRelease                              # -> app/build/outputs/apk/release/
cmake -S native/tests -B /tmp/fz && cmake --build /tmp/fz && /tmp/fz/test_core
```

## Known limits (v0.1)
* Not yet verified on real hardware: engine logic is unit-tested on the host against simulated acoustics only.
* Calibrate again when you change route (headphones, Bluetooth), volume, or mic mode.
* Keep the app in the foreground while recording (Android restricts background mic use).
* 10 minutes per track. Mono, 48 kHz.
* Speaker non-linearity and hand-held movement limit how much bleed can be removed; this is what the lab measures.
