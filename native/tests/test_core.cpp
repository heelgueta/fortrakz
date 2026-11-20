// Host-side tests: simulate a phone (speaker -> room -> mic) and check that the
// latency lab, echo-path estimation and the Studio's overdub alignment work.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <unistd.h>

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

// Full overdub: backing track plays, "singer" sings in time with what they hear,
// mic = speaker bleed + voice. The recorded track must line up with the backing.
static void runOverdub(int mode, const char* label) {
  printf("overdub end-to-end (%s)\n", label);
  const int RTL = 1234;
  char tmpl[] = "/tmp/fztestXXXXXX";
  char* dir = mkdtemp(tmpl);
  Studio st;
  st.setProjectDir(dir);

  // backing on track 0: 4 s of a chord
  const int64_t L = 4 * SR;
  std::vector<float> backing(static_cast<size_t>(L), 0.f);
  for (int64_t i = 0; i < L; i++)
    backing[size_t(i)] = 0.25f * std::sin(2 * M_PI * 220.0 * i / SR) + 0.15f * std::sin(2 * M_PI * 330.0 * i / SR);
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
  st.setLatencyFrames(RTL);
  st.setCleanMode(mode);

  // sing on track 1 while track 0 plays
  const int n = 192;
  const int64_t total = L;
  std::vector<float> outHist(size_t(total + RTL + 2 * n), 0.f);
  auto h = trueH();
  auto voice = [&](int64_t q) { return q < 0 ? 0.f : 0.3f * float(std::sin(2 * M_PI * 523.25 * q / SR)); };
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
      in[size_t(i)] = float(e * 0.25) + voice(k - RTL);  // singer in time with what they hear
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
}

int main() {
  testLatency();
  testEcho();
  testAdaptive();
  runOverdub(Studio::kCleanOff, "no cancellation");
  runOverdub(Studio::kCleanFixed, "fixed cancellation");
  runOverdub(Studio::kCleanAdaptive, "adaptive cancellation");
  printf(failures ? "\n%d FAILURES\n" : "\nALL OK\n", failures);
  return failures ? 1 : 0;
}
