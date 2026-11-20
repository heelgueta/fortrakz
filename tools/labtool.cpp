// Offline diagnosis of a lab echo capture using the same DSP core as the app.
//   labtool <stim.wav> <cap.wav>      (float32 mono wavs written by the lab)
// Prints delay drift, coherence per band (=ceiling for ANY linear canceller),
// and held-out ERLE for several filter lengths / strategies.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include "../native/core/dsp.h"
#include "../native/core/fft.h"
#include "../native/core/lab.h"
using namespace fz;

static std::vector<float> readWav(const char* p, int* sr) {
  FILE* f = fopen(p, "rb");
  std::vector<float> v;
  if (!f) return v;
  char id[4]; uint32_t sz;
  fseek(f, 12, SEEK_SET);
  while (fread(id, 1, 4, f) == 4 && fread(&sz, 4, 1, f) == 1) {
    if (!memcmp(id, "fmt ", 4)) { uint16_t fmt, ch; uint32_t r; fread(&fmt, 2, 1, f); fread(&ch, 2, 1, f); fread(&r, 4, 1, f); *sr = int(r); fseek(f, sz - 8, SEEK_CUR); }
    else if (!memcmp(id, "data", 4)) { v.resize(sz / 4); fread(v.data(), 4, v.size(), f); break; }
    else fseek(f, sz, SEEK_CUR);
  }
  fclose(f);
  return v;
}

int main(int argc, char** argv) {
  if (argc < 3) return 1;
  int sr = 48000;
  auto stim = readWav(argv[1], &sr);
  auto cap = readWav(argv[2], &sr);
  EchoStim es = makeEchoStim(sr);
  if (stim.size() != es.samples.size() || cap.size() < stim.size()) { printf("size mismatch %zu %zu\n", stim.size(), cap.size()); return 1; }
  const float* ref = stim.data() + es.noiseStart;
  const float* mic = cap.data() + es.noiseStart;
  const size_t N = size_t(es.noiseLen + es.tail), nl = size_t(es.noiseLen);

  // 1. delay at start vs end of the noise burst (clock drift shows up as lag change)
  auto lagAt = [&](size_t a, size_t n) { return findDelay(mic + a, n + 20000, ref + a, n, 20000).lag; };
  int d0 = lagAt(sr / 2, sr), d1 = lagAt(nl - 2 * size_t(sr), sr);
  printf("delay start %d, end %d  (drift %d samples over %.1fs)\n", d0, d1, d1 - d0, double(nl - 2.5 * sr) / sr);

  // 2. coherence per band, using delay-aligned reference
  int D = d0;
  const size_t B = 4096, hop = 2048;
  FftPlan plan(B);
  std::vector<double> sxx(B / 2), syy(B / 2), sxyr(B / 2), sxyi(B / 2);
  for (size_t a = size_t(sr); a + B + size_t(D) < nl; a += hop) {
    std::vector<cd> X(B), Y(B);
    for (size_t i = 0; i < B; i++) {
      double w = 0.5 - 0.5 * cos(2 * M_PI * i / B);
      X[i] = w * ref[a + i];
      Y[i] = w * mic[a + i + size_t(D)];
    }
    plan.run(X.data(), false); plan.run(Y.data(), false);
    for (size_t k = 0; k < B / 2; k++) {
      sxx[k] += std::norm(X[k]); syy[k] += std::norm(Y[k]);
      cd c = Y[k] * std::conj(X[k]); sxyr[k] += c.real(); sxyi[k] += c.imag();
    }
  }
  double edges[] = {100, 500, 1000, 2000, 4000, 8000, 16000, 24000};
  double totY = 0, totRes = 0;
  printf("band Hz        coh^2   echo-power-share  max ERLE(dB)\n");
  for (int b = 0; b < 7; b++) {
    size_t k0 = size_t(edges[b] * B / sr), k1 = size_t(edges[b + 1] * B / sr);
    double y = 0, res = 0, cs = 0;
    for (size_t k = k0; k < k1 && k < B / 2; k++) {
      double g = (sxyr[k] * sxyr[k] + sxyi[k] * sxyi[k]) / (sxx[k] * syy[k] + 1e-30);
      y += syy[k]; res += syy[k] * (1 - g); cs += g;
    }
    totY += y; totRes += res;
    printf("%5.0f-%-6.0f  %.3f   %6.3f            %5.1f\n", edges[b], edges[b + 1], cs / double(k1 - k0), 0.0, 10 * log10(y / (res + 1e-30)));
  }
  printf("overall linear ceiling (this path, ideal filter): %.1f dB\n\n", 10 * log10(totY / (totRes + 1e-30)));

  // 3. held-out ERLE for filter lengths
  size_t trainN = size_t(4 * sr);
  size_t u0 = trainN + sr / 20, u1 = nl;
  printf("taps   train  test   adaptive(test)  [train on first 4s, test on last 2s]\n");
  for (int taps : {128, 256, 512, 1024, 2048, 4096}) {
    EchoPath p = estimateEchoPath(ref, mic, trainN, size_t(sr) * 4 / 10, taps, 32, 4);
    std::vector<float> r(N), ra(N);
    cancelEcho(mic, ref, N, p, r.data(), 0, 0);
    cancelEcho(mic, ref, N, p, ra.data(), 128, 0.1f);
    printf("%5d  %5.1f  %5.1f  %5.1f\n", taps, erleDb(mic + sr / 5, r.data() + sr / 5, trainN - sr / 5), erleDb(mic + u0, r.data() + u0, u1 - u0), erleDb(mic + u0, ra.data() + u0, u1 - u0));
  }
  // 4. train on the LAST 4s, test on the FIRST 2s: is the path time-varying?
  {
    std::vector<float> rr(ref + 2 * sr, ref + nl), mm(mic + 2 * sr, mic + nl);
    EchoPath p = estimateEchoPath(rr.data(), mm.data(), rr.size(), size_t(sr) * 4 / 10, 1024, 32, 4);
    std::vector<float> r(N);
    cancelEcho(mic, ref, N, p, r.data(), 0, 0);
    printf("train last 4s -> test first 2s (1024 taps): %.1f dB;  in-sample %.1f dB\n", erleDb(mic + sr / 5, r.data() + sr / 5, 2 * sr - sr / 5), erleDb(mic + 3 * sr, r.data() + 3 * sr, 2 * sr));
  }
  return 0;
}
