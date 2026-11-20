#!/usr/bin/env python3
"""Summarise a Fortrakz lab result (zip or folder containing results.json).

    python3 tools/analyze.py ~/Downloads/fortrakz-lab-run-....zip

Stdlib only. Prints one row per run plus the device block, so you can paste it
(or just send me the zip) and we can pick the best mic mode and tune the
cancellation from real numbers.
"""
import json, statistics, sys, zipfile, pathlib

PRESETS = {1: "generic", 5: "camcorder", 6: "voice_rec", 7: "voice_comm", 9: "unprocessed"}
PERF = {10: "default", 12: "lowlat"}


def load(path):
    p = pathlib.Path(path).expanduser()
    if p.is_dir():
        f = next(p.rglob("results.json"))
        return json.loads(f.read_text())
    with zipfile.ZipFile(p) as z:
        name = next(n for n in z.namelist() if n.endswith("results.json"))
        return json.loads(z.read(name))


def g(d, *keys, default=None):
    for k in keys:
        if not isinstance(d, dict) or k not in d:
            return default
        d = d[k]
    return d


def fmt(v, spec="{:.1f}", dash="  -  "):
    return dash if v is None else spec.format(v)


def main(path):
    r = load(path)
    dev = r.get("device", {})
    print("DEVICE:", dev.get("manufacturer"), dev.get("model"), "| Android", dev.get("android"),
          "| native sr/buf", dev.get("native_sample_rate"), dev.get("native_frames_per_buffer"),
          "| low-latency feature", dev.get("feature_low_latency"), "| pro", dev.get("feature_pro"),
          "| volume", f"{dev.get('music_volume')}/{dev.get('music_volume_max')}")
    print("ROUTES:", dev.get("audio_devices"))
    print()
    hdr = f"{'tag':<16}{'mic mode':<12}{'perf':<8}{'api':<8}{'burst':>6} {'RTL ms':>8}{'±ms':>6}{'heard':>7}{'floor dB':>9}{'SNR dB':>7}{'ERLE':>6}{'ERLE+ad':>8}  notes"
    print(hdr)
    print("-" * len(hdr))
    by_cfg = {}
    for run in r.get("runs", []):
        lat, echo = run.get("latency", {}), run.get("echo")
        lres, eres = lat.get("result", {}), (echo or {}).get("result", {})
        preset, perf = lat.get("preset"), lat.get("perf")
        st = lat.get("stream", {})
        med = lres.get("median_ms")
        sd = (lres.get("std_frames") / 48.0) if lres.get("std_frames") is not None else None
        notes = []
        if st.get("in_fallback"):
            notes.append("mic mode refused->generic")
        elif st.get("in_preset_actual") not in (None, preset):
            notes.append(f"actual preset {st.get('in_preset_actual')}")
        if g(st, "in_perf") != perf:
            notes.append("perf downgraded")
        if st.get("callback_xruns", 0) > 0:
            notes.append(f"{st['callback_xruns']} xruns")
        if eres and not eres.get("ok"):
            notes.append(eres.get("error", "echo failed"))
        print(f"{run.get('tag',''):<16}{PRESETS.get(preset, preset)!s:<12}{PERF.get(perf, perf)!s:<8}"
              f"{str(st.get('api','?')):<8}{st.get('out_burst', 0):>6} "
              f"{fmt(med):>8}{fmt(sd):>6}{str(lres.get('valid','-'))+'/'+str(lres.get('sent','-')):>7}"
              f"{fmt(eres.get('noise_floor_db') if eres else lres.get('noise_floor_db')):>9}{fmt(eres.get('snr_db')):>7}"
              f"{fmt(eres.get('erle_test_db'), '{:.0f}'):>6}{fmt(eres.get('erle_test_adaptive_db'), '{:.0f}'):>8}  {'; '.join(notes)}")
        if med is not None:
            by_cfg.setdefault((preset, perf), []).append(med)
    for (preset, perf), v in by_cfg.items():
        if len(v) > 1:
            print(f"\nrestart jitter {PRESETS.get(preset, preset)}/{PERF.get(perf, perf)}: "
                  f"{len(v)} sessions, median RTL spread {max(v) - min(v):.2f} ms (stdev {statistics.pstdev(v):.2f} ms)")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
