# Lab notes (real-device findings)

## Device 1: Samsung Galaxy S23 Ultra (SM-S918B), Android 16, 2026-11-20
First lab round (matrix, jitter, quick) on v0.1.0.

| finding | evidence | consequence |
|---|---|---|
| Low-latency mode gave a **dead mic** in unprocessed / voice-recognition / generic (3000+ xruns, floor -90..-100 dB) | matrix `p9_m12`, `p6_m12`, `p1_m12` | default is now "Default" mode; calibration auto-falls back through other modes |
| Default mode: 1600-frame output bursts, **round trip ~277 ms** | all `m10` runs | harmless for overdub (compensated), irrelevant for live monitoring |
| Bleed cancellation only 4-18 dB | `erle_test_db` | see next rows |
| Coherence ceiling for *any linear filter* only 5-9 dB on captures | `tools/labtool.cpp` | the mic was not a stable linear function of the playback |
| **128-256 frame delay jumps** and runs of **zero-filled frames** inside a capture | `tools/blocktrack.cpp` (`p9`: +128 at 1.75 s; `p1`: 1408 zero frames) | root cause: old stream code dropped/zero-filled input to "catch up". Replaced by a rigid ring-buffer aligner (`native/core/aligner.h`) |
| Restart-to-restart RTL spread ~4 ms (stdev 1.6 ms) | jitter run | session offset differs a little; each take now measures its own delay |
| Voice-communication mode: -200 dB floor, 188 dB "SNR" | `p7_*` | platform AEC/gating zeroes the mic when idle; not usable as a plain recorder |
| Camcorder/voice-rec clip heavily at max volume | `p6_m10` 48% clipped | calibrate at a moderate volume; lab now reports `clip_pct` |

## What changed because of this (v0.2.0)
1. Rigid input/output alignment (no drops, no zero-fill).
2. **Learn from the take**: the speaker->mic path *and its exact delay* are estimated from each take, using the music that was actually playing (at its real level) as excitation, iteratively reweighted so loud-voice blocks count less.
3. "Learn + re-learn every 15 s" for long takes (follows slow drift).
4. Lab reports capture health: `clip_pct`, `zero_frames`, `lag_jumps/lag_dev_max`.
5. Lab can export the last real take (raw mic + playback) for offline tuning.

## To check next round
* Lab health numbers on the new engine: `lag_jumps` should be 0, `zero_frames` 0.
* Does low-latency mode work now that input has its own callback? (matrix `m12` rows)
* Real-take diagnostics: `fit_peak_ratio` (delay confidence), `reduction_db`, and the shared take zip.
