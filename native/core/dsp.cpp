#include "dsp.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "fft.h"

namespace fz {

// ---- generators ---------------------------------------------------------------

std::vector<float> makeChirp(int sr, double f0, double f1, double seconds, float amp) {
  size_t n = size_t(seconds * sr);
  std::vector<float> y(n);
  double k = (f1 - f0) / seconds;
  for (size_t i = 0; i < n; i++) {
    double t = double(i) / sr;
    double ph = 2.0 * M_PI * (f0 * t + 0.5 * k * t * t);
    double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * double(i) / double(n - 1));  // Hann
    y[i] = float(amp * w * std::sin(ph));
  }
  return y;
}

std::vector<float> makeNoise(size_t n, uint32_t seed, float rms) {
  std::vector<float> y(n);
  uint32_t s = seed ? seed : 0x9E3779B9u;
  auto u = [&]() {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return double(s) / 4294967296.0;
  };
  for (size_t i = 0; i < n; i++) {
    double g = -6.0;
    for (int k = 0; k < 12; k++) g += u();  // ~N(0,1)
    double v = g * rms;
    v = std::max(-0.95, std::min(0.95, v));
    y[i] = float(v);
  }
  return y;
}

// ---- correlation ----------------------------------------------------------------

std::vector<double> xcorrPositive(const float* a, size_t na, const float* b, size_t nb,
                                  size_t maxLag) {
  if (na == 0 || nb == 0) return {};
  size_t N = nextPow2(na + nb);
  FftPlan plan(N);
  std::vector<cd> A(N), B(N);
  for (size_t i = 0; i < na; i++) A[i] = a[i];
  for (size_t i = 0; i < nb; i++) B[i] = b[i];
  plan.run(A.data(), false);
  plan.run(B.data(), false);
  for (size_t i = 0; i < N; i++) A[i] *= std::conj(B[i]);
  plan.run(A.data(), true);
  size_t m = std::min(maxLag + 1, na);
  std::vector<double> c(m);
  for (size_t k = 0; k < m; k++) c[k] = A[k].real();
  return c;
}

DelayResult findDelay(const float* sig, size_t ns, const float* ref, size_t nr, size_t maxLag) {
  DelayResult r;
  if (ns == 0 || nr == 0) return r;
  std::vector<double> c = xcorrPositive(sig, ns, ref, nr, maxLag);
  if (c.empty()) return r;
  size_t best = 0;
  double bv = 0, sum = 0;
  for (size_t k = 0; k < c.size(); k++) {
    double v = std::fabs(c[k]);
    sum += v;
    if (v > bv) {
      bv = v;
      best = k;
    }
  }
  double eRef = 0, eWin = 0;
  for (size_t i = 0; i < nr; i++) eRef += double(ref[i]) * ref[i];
  for (size_t i = 0; i < nr && best + i < ns; i++) eWin += double(sig[best + i]) * sig[best + i];
  r.lag = int(best);
  r.coherence = bv / std::sqrt(eRef * eWin + 1e-30);
  r.ratio = bv / (sum / double(c.size()) + 1e-30);
  return r;
}

// ---- convolution -------------------------------------------------------------------

void convolveCausal(const float* x, size_t n, const float* h, size_t L, float* y) {
  std::memset(y, 0, n * sizeof(float));
  if (n == 0 || L == 0) return;
  size_t N = nextPow2(std::max<size_t>(2 * L, 4096));
  size_t B = N - L + 1;
  FftPlan plan(N);
  std::vector<cd> H(N), X(N);
  for (size_t i = 0; i < L; i++) H[i] = h[i];
  plan.run(H.data(), false);
  for (size_t start = 0; start < n; start += B) {
    size_t len = std::min(B, n - start);
    std::fill(X.begin(), X.end(), cd(0, 0));
    for (size_t i = 0; i < len; i++) X[i] = x[start + i];
    plan.run(X.data(), false);
    for (size_t i = 0; i < N; i++) X[i] *= H[i];
    plan.run(X.data(), true);
    size_t outLen = std::min(N, n - start);
    for (size_t i = 0; i < outLen; i++) y[start + i] += float(X[i].real());
  }
}

// ---- echo path ----------------------------------------------------------------------

EchoPath estimateEchoPath(const float* ref, const float* mic, size_t n, size_t maxDelay,
                          int taps, int pre, int iters) {
  EchoPath p;
  if (n < size_t(taps) * 4) return p;
  DelayResult d = findDelay(mic, n, ref, n, maxDelay);
  if (d.lag < 0) return p;
  p.delay = std::max(0, d.lag - pre);
  std::vector<float> x(n, 0.f);
  for (size_t j = size_t(p.delay); j < n; j++) x[j] = ref[j - size_t(p.delay)];
  double Ex = 0;
  for (size_t j = 0; j < n; j++) Ex += double(x[j]) * x[j];
  if (Ex < 1e-9) return EchoPath();
  p.h.assign(size_t(taps), 0.f);
  std::vector<float> r(mic, mic + n), est(n);
  for (int it = 0; it < iters; it++) {
    if (it > 0) {
      convolveCausal(x.data(), n, p.h.data(), p.h.size(), est.data());
      for (size_t j = 0; j < n; j++) r[j] = mic[j] - est[j];
    }
    std::vector<double> c = xcorrPositive(r.data(), n, x.data(), n, size_t(taps) - 1);
    for (size_t k = 0; k < c.size() && k < p.h.size(); k++) p.h[k] += float(c[k] / Ex);
  }
  return p;
}

void cancelEcho(const float* mic, const float* ref, size_t n, const EchoPath& p, float* out,
                int adaptTaps, float mu) {
  if (!p.valid()) {
    std::memcpy(out, mic, n * sizeof(float));
    return;
  }
  std::vector<float> x(n, 0.f), est(n);
  for (size_t j = size_t(p.delay); j < n; j++) x[j] = ref[j - size_t(p.delay)];
  convolveCausal(x.data(), n, p.h.data(), p.h.size(), est.data());
  for (size_t j = 0; j < n; j++) out[j] = mic[j] - est[j];
  if (adaptTaps <= 0) return;

  const size_t T = size_t(adaptTaps);
  std::vector<float> xp(n + T, 0.f);  // xp[T + j] = x[j]
  std::memcpy(xp.data() + T, x.data(), n * sizeof(float));
  std::vector<float> wr(T, 0.f);  // reversed weights: y = sum_m wr[m] * xp[j+1+m]
  const double a = 1.0 / 480.0;   // ~10 ms smoothing @48k
  double eRes = 0, eEst = 0, pw = 0;
  for (size_t j = 0; j < n; j++) {
    pw += double(x[j]) * x[j];
    if (j >= T) pw -= double(x[j - T]) * x[j - T];
    const float* win = xp.data() + j + 1;
    float y = 0.f;
    for (size_t m = 0; m < T; m++) y += wr[m] * win[m];
    float e = out[j] - y;
    eRes = (1 - a) * eRes + a * double(out[j]) * out[j];
    eEst = (1 - a) * eEst + a * double(est[j]) * est[j];
    out[j] = e;
    if (eEst > 1e-8 && eRes < eEst) {
      float g = mu * e / float(pw + 1e-6);
      for (size_t m = 0; m < T; m++) wr[m] += g * win[m];
    }
  }
}

double powerDb(const float* x, size_t n) {
  double s = 0;
  for (size_t i = 0; i < n; i++) s += double(x[i]) * x[i];
  return 10.0 * std::log10(s / double(std::max<size_t>(n, 1)) + 1e-20);
}

double erleDb(const float* mic, const float* res, size_t n) {
  return powerDb(mic, n) - powerDb(res, n);
}

bool saveEchoPath(const std::string& path, const EchoPath& p) {
  FILE* f = fopen(path.c_str(), "wb");
  if (!f) return false;
  int32_t hdr[2] = {p.delay, int32_t(p.h.size())};
  fwrite(hdr, 4, 2, f);
  fwrite(p.h.data(), 4, p.h.size(), f);
  fclose(f);
  return true;
}

bool loadEchoPath(const std::string& path, EchoPath& p) {
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) return false;
  int32_t hdr[2];
  bool ok = fread(hdr, 4, 2, f) == 2 && hdr[1] > 0 && hdr[1] <= 8192 && hdr[0] >= 0;
  if (ok) {
    p.delay = hdr[0];
    p.h.resize(size_t(hdr[1]));
    ok = fread(p.h.data(), 4, p.h.size(), f) == p.h.size();
  }
  fclose(f);
  if (!ok) p = EchoPath();
  return ok;
}

// ---- wav ---------------------------------------------------------------------------------

static void le32(FILE* f, uint32_t v) { fwrite(&v, 4, 1, f); }
static void le16(FILE* f, uint16_t v) { fwrite(&v, 2, 1, f); }

static bool writeWavRaw(const std::string& path, const void* data, size_t bytes, int sr,
                        uint16_t fmt, uint16_t bits) {
  FILE* f = fopen(path.c_str(), "wb");
  if (!f) return false;
  fwrite("RIFF", 1, 4, f);
  le32(f, uint32_t(36 + bytes));
  fwrite("WAVEfmt ", 1, 8, f);
  le32(f, 16);
  le16(f, fmt);
  le16(f, 1);
  le32(f, uint32_t(sr));
  le32(f, uint32_t(sr * bits / 8));
  le16(f, uint16_t(bits / 8));
  le16(f, bits);
  fwrite("data", 1, 4, f);
  le32(f, uint32_t(bytes));
  fwrite(data, 1, bytes, f);
  fclose(f);
  return true;
}

bool writeWavMono16(const std::string& path, const int16_t* data, size_t n, int sr) {
  return writeWavRaw(path, data, n * 2, sr, 1, 16);
}
bool writeWavMonoFloat(const std::string& path, const float* data, size_t n, int sr) {
  return writeWavRaw(path, data, n * 4, sr, 3, 32);
}

}  // namespace fz
