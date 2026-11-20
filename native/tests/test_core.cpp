// Host-side tests: simulate a phone (speaker -> room -> mic) and check that the
// latency lab, echo-path estimation and the Studio's overdub alignment work.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <unistd.h>

#include "../core/aligner.h"
#include "../core/dsp.h"
#include "../core/lab.h"
#include "../core/studio.h"

using namespace fz;
static int failures = 0;
#define CHECK(c, ...)                                  \
  do {                                                 \
    if (!(c)) {                                        \
      failures++;                                      \
      printf("  FAIL %s:%d  %s  ", __FILE__, __LINE__, #c); \
      printf(__VA_ARGS__);                             \
      printf("\n");                                    \
    }                                                  \
  } while (0)

static const int SR = 48000;

// Fake acoustic path: delay + sparse impulse response (+ optional tanh).
static std::vector<float> trueH() {
  std::vector<float> h(400, 0.f);
  h[0] = 1.0f; h[7] = -0.35f; h[40] = 0.2f; h[111] = 0.12f; h[250] = -0.06f; h[399] = 0.03f;
  return h;
}
static std::vector<float> room(const std::vector<float>& x, int delay, float gain, float noiseRms,
                               bool nonlinear = false, float changeAt = -1, float gain2 = 0) {
  auto h = trueH();
  std::vector<float> y(x.size()), d(x.size(), 0.f);
  for (size_t i = delay; i < x.size(); i++) d[i] = x[i - delay];
  convolveCausal(d.data(), d.size(), h.data(), h.size(), y.data());
  auto nz = makeNoise(x.size(), 777, noiseRms);
  for (size_t i = 0; i < y.size(); i++) {
    float g = (changeAt >= 0 && i > size_t(changeAt)) ? gain2 : gain;
    y[i] = y[i] * g;
    if (nonlinear) y[i] = std::tanh(3.f * y[i]) / 3.f;
    y[i] += nz[i];
  }
  return y;
}

static void testLatency() {
  printf("latency analysis\n");
  LatencyStim s = makeLatencyStim(SR);
  auto cap = room(s.samples, 1234, 0.3f, 0.002f);
  LatencyAnalysis a = analyzeLatency(s, cap, SR);
  printf("  %s\n", a.json.c_str());
  CHECK(a.ok, "analysis failed");
  CHECK(std::fabs(a.medianFrames - 1234) <= 1, "median %f", a.medianFrames);
  // silence => must not hallucinate
  std::vector<float> quiet(s.samples.size(), 0.f);
  auto qn = makeNoise(quiet.size(), 5, 0.002f);
  LatencyAnalysis q = analyzeLatency(s, qn, SR);
  CHECK(!q.ok, "should reject noise-only capture");
}

static void testEcho() {
  printf("echo path + ERLE\n");
  EchoStim s = makeEchoStim(SR);
  auto cap = room(s.samples, 1234, 0.25f, 0.002f);
  EchoAnalysis a = analyzeEcho(s, cap, SR);
  printf("  %s\n", a.json.c_str());
  CHECK(a.ok, "echo analysis failed");
  CHECK(a.path.delay + 32 >= 1233 && a.path.delay + 32 <= 1235, "delay %d", a.path.delay + 32);
  // nonlinear speaker: still should get something, just less
  auto cap2 = room(s.samples, 1234, 0.25f, 0.002f, true);
  EchoAnalysis b = analyzeEcho(s, cap2, SR);
  printf("  nonlinear: %s\n", b.json.c_str());
  // dead mic
  std::vector<float> dead(cap.size());
  auto dn = makeNoise(dead.size(), 9, 0.002f);
  EchoAnalysis c = analyzeEcho(s, dn, SR);
  CHECK(!c.ok, "dead mic should fail");
}

static void testAdaptive() {
  printf("adaptive stage tracks a changing path\n");
  auto ref = makeNoise(size_t(6 * SR), 42, 0.2f);
  auto mic = room(ref, 500, 0.25f, 0.001f, false, float(3 * SR), 0.32f);
  EchoPath p = estimateEchoPath(ref.data(), mic.data(), size_t(2 * SR), 20000, 1024, 32, 3);
  std::vector<float> fx(ref.size()), ad(ref.size());
  cancelEcho(mic.data(), ref.data(), ref.size(), p, fx.data(), 0, 0);
  cancelEcho(mic.data(), ref.data(), ref.size(), p, ad.data(), 128, 0.1f);
  size_t a = size_t(4 * SR), n = size_t(2 * SR);
  double ef = erleDb(mic.data() + a, fx.data() + a, n), ea = erleDb(mic.data() + a, ad.data() + a, n);
  printf("  after path change: fixed %.1f dB, adaptive %.1f dB\n", ef, ea);
  CHECK(ea > ef + 2.0, "adaptive didn't help");
}

// Music-like excitation: harmonic tones + low-passed noise + slow amplitude modulation.
static std::vector<float> musicLike(size_t n, uint32_t seed) {
  auto nz = makeNoise(n, seed, 0.1f);
  std::vector<float> y(n);
  float lp = 0;
  for (size_t i = 0; i < n; i++) {
    lp += 0.2f * (nz[i] - lp);
    double t = double(i) / SR;
    double tone = 0.18 * std::sin(2 * M_PI * 196 * t) + 0.12 * std::sin(2 * M_PI * 392 * t + 1) +
                  0.08 * std::sin(2 * M_PI * 587 * t + 2);
    y[i] = float((tone + 1.5 * lp) * (0.6 + 0.4 * std::sin(2 * M_PI * 0.7 * t)));
  }
  return y;
}

static void testAligner() {
  printf("input aligner: no drops, no shifts even with bursty input\n");
  InputAligner al(1 << 16);
  al.reset();
  const int n = 480;
  std::vector<float> out(n);
  int64_t inIdx = 0, mismatches = 0;
  // input arrives in irregular bursts (96..1600 frames), output asks for 480 each time
  uint32_t r = 1;
  int64_t K = 0; bool haveK = false;
  for (int cb = 0; cb < 400; cb++) {
    r = r * 1664525u + 1013904223u;
    int burst = 96 + int(r >> 20) % 1500;
    if (cb % 3 != 2) {  // sometimes the input callback hasn't run yet
      std::vector<float> chunk(static_cast<size_t>(burst), 0.f);
      for (int i = 0; i < burst; i++) chunk[size_t(i)] = float(inIdx + i);  // value == absolute index
      al.write(chunk.data(), burst);
      inIdx += burst;
    }
    al.read(out.data(), n, 2000);
    if (cb == 0) { K = al.offset(); haveK = true; }
    if (cb > 3 && haveK)
      for (int i = 0; i < n; i++) {
        float exp = float(int64_t(cb) * n + i + K);
        if (out[size_t(i)] != 0.f && out[size_t(i)] != exp) mismatches++;
      }
  }
  printf("  late frames %lld, resyncs %d, mismatches %lld\n", (long long)al.lateFrames(), al.resyncs(), (long long)mismatches);
  CHECK(mismatches == 0, "mapping moved");
}

static void testFit() {
  printf("per-take path fit on music-like reference, with a voice present\n");
  const size_t n = size_t(30 * SR);
  auto ref = musicLike(n, 99);
  const int D = 13037;
  auto mic = room(ref, D, 0.3f, 0.002f, true);  // slightly nonlinear speaker
  for (size_t i = 0; i < n; i++) { double t = double(i) / SR; mic[i] += 0.25f * float((std::fmod(t, 2.5) < 1.5 ? 1.0 : 0.0) * std::sin(2 * M_PI * 440.0 * t + 3 * std::sin(2 * M_PI * 5.5 * t))); }
  std::vector<int16_t> r16(n), m16(n);
  for (size_t i = 0; i < n; i++) { r16[i] = toI16(ref[i]); m16[i] = toI16(mic[i]); }
  PathFit f = fitPathI16(r16.data(), m16.data(), n, 13000, 3500, 1024, 32);
  printf("  ok %d delay %d (true %d) peakRatio %.1f voice-diluted ceiling %.1f dB\n", f.ok, f.delay, D, f.peakRatio, f.ceilingDb);
  CHECK(f.ok, "fit not ok");
  CHECK(std::abs(f.delay - D) <= 1, "delay %d", f.delay);
  // cancel and measure on a silent-voice copy
  auto mic2 = room(ref, D, 0.3f, 0.002f, true);
  std::vector<float> res(n);
  cancelEcho(mic2.data(), ref.data(), n, f.path, res.data(), 0, 0);
  double e = erleDb(mic2.data() + SR, res.data() + SR, n - SR);
  printf("  ERLE with the learned filter: %.1f dB\n", e);
  CHECK(e > 12.0, "ERLE %.1f", e);
}

static void testCountIn() {
  printf("count-in click\n");
  Studio st;
  st.setClick(120, 2, false, true);  // 2 beats @120 = 1.0 s
  st.play();
  CHECK(st.state() == Studio::kCountIn, "state %d", st.state());
  std::vector<float> in(480, 0.f), out(480);
  double clickEnergy = 0; int blocks = 0;
  while (st.state() == Studio::kCountIn && blocks < 1000) {
    st.process(in.data(), out.data(), 480);
    for (float v : out) clickEnergy += double(v) * v;
    blocks++;
  }
  printf("  count-in lasted %d blocks (%.3f s), energy %.2f, pos %lld\n", blocks, blocks * 480.0 / SR, clickEnergy, (long long)st.position());
  CHECK(std::abs(blocks * 480.0 / SR - 1.0) < 0.011, "length");
  CHECK(clickEnergy > 1.0, "clicks audible");
  CHECK(st.position() == 0, "position must not advance during count-in");
  st.stop();
}

// Full overdub: backing track plays, "singer" sings in time with what they hear,
// mic = speaker bleed + voice. The recorded track must line up with the backing.
static double runOverdub(int mode, const char* label, int priorErr = 0, int seconds = 4, bool drift = false) {
  printf("overdub end-to-end (%s)\n", label);
  const int RTL = 1234;
  char tmpl[] = "/tmp/fztestXXXXXX";
  char* dir = mkdtemp(tmpl);
  Studio st;
  st.setProjectDir(dir);

  // backing on track 0: 4 s of a chord
  const int64_t L = int64_t(seconds) * SR;
  std::vector<float> backing(static_cast<size_t>(L), 0.f);
  {
    auto mus = musicLike(size_t(L), 4242);  // music is never two pure tones
    for (int64_t i = 0; i < L; i++)
      backing[size_t(i)] = mus[size_t(i)] + 0.25f * std::sin(2 * M_PI * 220.0 * i / SR);
  }
  {
    // inject via a first "recording" with clean mode off so the test only uses the public API
    st.setCleanMode(Studio::kCleanOff);
    st.setLatencyFrames(0);
    st.record(0);
    std::vector<float> o(192);
    for (int64_t j = 0; j < L; j += 192) st.process(backing.data() + j, o.data(), 192);
    st.stop();
    st.finalizeTake();
  }
  CHECK(st.trackLength(0) >= L - 192, "backing length %lld", (long long)st.trackLength(0));

  // calibrate echo path through the same fake room
  EchoStim es = makeEchoStim(SR);
  auto ecap = room(es.samples, RTL, 0.25f, 0.002f);
  EchoAnalysis ea = analyzeEcho(es, ecap, SR);
  CHECK(ea.ok, "calibration");
  st.setEchoPath(ea.path);
  st.setLatencyFrames(RTL + priorErr);  // a stale calibration: per-take learning must correct it
  st.setCleanMode(mode);

  // sing on track 1 while track 0 plays
  const int n = 192;
  const int64_t total = L;
  std::vector<float> outHist(size_t(total + RTL + 2 * n), 0.f);
  auto h = trueH();
  auto voice = [&](int64_t q) {
    if (q < 0) return 0.f;
    double t = double(q) / SR, gate = std::fmod(t, 2.0) < 1.4 ? 1.0 : 0.0;  // phrases with pauses
    return 0.3f * float(gate * std::sin(2 * M_PI * 523.25 * t + 2.0 * std::sin(2 * M_PI * 5.0 * t)));
  };
  st.seek(0);
  st.record(1);
  std::vector<float> in(n), out(n);
  for (int64_t j = 0; j < total; j += n) {
    for (int i = 0; i < n; i++) {
      int64_t k = j + i;
      double e = 0;
      for (size_t t = 0; t < h.size(); t++) {
        int64_t idx = k - RTL - int64_t(t);
        if (idx >= 0) e += h[t] * outHist[size_t(idx)];
      }
      const double g = (drift && k > total / 2) ? 0.42 : 0.25;  // hand moved / volume changed halfway
      in[size_t(i)] = float(e * g) + voice(k - RTL);  // singer in time with what they hear
    }
    st.process(in.data(), out.data(), n);
    for (int i = 0; i < n; i++) outHist[size_t(j + i)] = out[size_t(i)];
  }
  bool pending = st.stop();
  CHECK(pending, "take should be pending");
  std::string rep = st.finalizeTake();
  printf("  %s\n", rep.c_str());

  // compare track 1 against the ideal voice
  const int16_t* dummy = nullptr; (void)dummy;
  st.setCleanMode(Studio::kCleanOff);
  std::string wav = std::string(dir) + "/mix.wav";
  st.setTrack(0, 0.f, true, false);  // mute backing, hear only track 1
  CHECK(st.exportMix(wav), "export");
  FILE* f = fopen(wav.c_str(), "rb");
  std::vector<int16_t> pcm;
  if (f) {
    fseek(f, 44, SEEK_SET);
    int16_t v;
    while (fread(&v, 2, 1, f) == 1) pcm.push_back(v);
    fclose(f);
  }
  double ee = 0, ev = 0, dot = 0, ey = 0;
  size_t a = SR / 2, b = std::min<size_t>(pcm.size(), size_t(total - RTL - SR / 2));
  for (size_t i = a; i < b; i++) {
    double y = pcm[i] / 32768.0 / 0.8;  // undo track gain
    double r = voice(int64_t(i));
    ee += (y - r) * (y - r);
    ev += r * r;
    dot += y * r;
    ey += y * y;
  }
  double snrDb = 10 * std::log10(ev / (ee + 1e-20));
  double corr = dot / std::sqrt(ev * ey + 1e-20);
  printf("  voice vs track: corr %.4f, error %.1f dB below voice\n", corr, snrDb);
  if (drift) return snrDb;
  if (mode == Studio::kCleanOff) {
    CHECK(corr > 0.5 && corr < 0.999, "sanity (bleed expected)");
  } else {
    CHECK(corr > 0.995, "alignment/cancel corr %.4f", corr);
    CHECK(snrDb > 25.0, "residual too high: %.1f dB", snrDb);
  }
  // cleanup
  char cmd[256];
  snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
  (void)!system(cmd);
  return snrDb;
}

int main() {
  testLatency();
  testEcho();
  testAdaptive();
  testAligner();
  testFit();
  testCountIn();
  runOverdub(Studio::kCleanOff, "no cancellation");
  runOverdub(Studio::kCleanFixed, "fixed cancellation");
  runOverdub(Studio::kCleanAdaptive, "adaptive cancellation");
  runOverdub(Studio::kCleanLearn, "learn from take, calibration off by 37 frames", 37);
  runOverdub(Studio::kCleanLearnAdaptive, "learn+adaptive, calibration off by -61 frames", -61);
  {
    double g = runOverdub(Studio::kCleanLearn, "40 s take, path gain jumps +4.5 dB halfway: one global filter", 0, 40, true);
    double sgm = runOverdub(Studio::kCleanLearnAdaptive, "40 s take, path gain jumps +4.5 dB halfway: re-learned every 15 s", 0, 40, true);
    printf("  global %.1f dB vs segmented %.1f dB\n", g, sgm);
    CHECK(sgm > g + 3.0, "segmenting should help when the path drifts");
    CHECK(sgm > 25.0, "segmented residual %.1f", sgm);
  }
  printf(failures ? "\n%d FAILURES\n" : "\nALL OK\n", failures);
  return failures ? 1 : 0;
}
