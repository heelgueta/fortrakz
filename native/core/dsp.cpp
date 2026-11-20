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
  const double aFast = 1.0 / 48.0;  // ~1 ms: reacts to a voice onset before the slow one can
  double eRes = 0, eFast = 0, eEst = 0, pw = 0;
  int64_t hold = 0;  // frames to stay frozen after voice was detected
  const int64_t holdLen = 14400;  // 300 ms
  for (size_t j = 0; j < n; j++) {
    pw += double(x[j]) * x[j];
    if (j >= T) pw -= double(x[j - T]) * x[j - T];
    const float* win = xp.data() + j + 1;
    float y = 0.f;
    for (size_t m = 0; m < T; m++) y += wr[m] * win[m];
    float e = out[j] - y;
    const double r2 = double(out[j]) * out[j];
    eRes = (1 - a) * eRes + a * r2;
    eFast = (1 - aFast) * eFast + aFast * r2;
    eEst = (1 - a) * eEst + a * double(est[j]) * est[j];
    out[j] = e;
    if (eFast > 0.5 * eEst || eRes > 0.5 * eEst) hold = holdLen;
    if (hold > 0) {
      hold--;
      continue;
    }
    if (eEst > 1e-8) {
      float g = mu * e / float(pw + 1e-6);
      for (size_t m = 0; m < T; m++) wr[m] += g * win[m];
    }
  }
}

template <class TR, class TM>
static PathFit fitPathT(const TR* ref, const TM* mic, size_t n, float sR, float sM, int prior, int half,
                        int taps, int pre) {
  PathFit r;
  half = std::min(half, 4000);
  taps = std::min(taps, 2048);
  if (n < 65536 || prior < 0) return r;
  auto R = [&](int64_t i) -> float { return (i < 0 || i >= int64_t(n)) ? 0.f : float(ref[i]) * sR; };
  auto M = [&](int64_t i) -> float { return (i < 0 || i >= int64_t(n)) ? 0.f : float(mic[i]) * sM; };

  // ---- stage 1: delay by averaged PHAT cross-correlation -------------------------------
  {
    const size_t B = 16384, hop = B / 2;
    FftPlan plan(B);
    const int64_t s = std::max(0, prior - 4096);
    const int e = prior - int(s);  // expected lag in the shifted domain
    std::vector<cd> acc(B, cd(0, 0)), X(B), Y(B);
    const size_t total = (n > size_t(s) + B) ? (n - size_t(s) - B) / hop : 0;
    const size_t stride = std::max<size_t>(1, total / 400);
    size_t used = 0;
    for (size_t b = 0; b < total; b += stride) {
      const int64_t j0 = s + int64_t(b * hop);
      double er = 0;
      for (size_t i = 0; i < B; i++) {
        double w = 0.5 - 0.5 * std::cos(2 * M_PI * double(i) / double(B));
        float x = R(j0 + int64_t(i) - s), y = M(j0 + int64_t(i));
        X[i] = w * x;
        Y[i] = w * y;
        er += double(x) * x;
      }
      if (er / double(B) < 1e-8) continue;  // silent reference: nothing to learn
      plan.run(X.data(), false);
      plan.run(Y.data(), false);
      for (size_t k = 0; k < B; k++) {
        cd c = Y[k] * std::conj(X[k]);
        acc[k] += c / (std::abs(c) + 1e-12);
      }
      used++;
    }
    if (used < 4) return r;
    plan.run(acc.data(), true);
    double sum = 0, best = 0;
    int bestLag = 0;
    for (size_t k = 0; k < B; k++) sum += std::abs(acc[k]);
    for (int lag = e - half; lag <= e + half; lag++) {
      int idx = ((lag % int(B)) + int(B)) % int(B);
      double v = std::abs(acc[size_t(idx)]);
      if (v > best) {
        best = v;
        bestLag = lag;
      }
    }
    r.delay = int(s) + bestLag;
    r.peakRatio = best / (sum / double(B) + 1e-30);
  }

  // ---- stage 2: transfer function H1 = Sxy / Sxx, iteratively reweighted ---------------
  // Blocks where the mic is NOT explained by the playback (a loud voice) get small weights,
  // so the filter is learned mostly from the gaps between phrases.
  {
    const size_t B = 8192, hop = B / 2;
    FftPlan plan(B);
    const int d0 = std::max(0, r.delay - pre);
    const size_t total = n > B + size_t(d0) ? (n - B - size_t(d0)) / hop : 0;
    const size_t stride = std::max<size_t>(1, total / 600);
    std::vector<cd> X(B), Y(B), H(B, cd(0, 0));
    std::vector<double> sxx(B), syy(B);
    std::vector<cd> sxy(B);
    double tot = 0, res = 0;
    for (int pass = 0; pass < 4; pass++) {
      std::fill(sxx.begin(), sxx.end(), 0.0);
      std::fill(syy.begin(), syy.end(), 0.0);
      std::fill(sxy.begin(), sxy.end(), cd(0, 0));
      size_t used = 0;
      for (size_t b = 0; b < total; b += stride) {
        const int64_t j0 = int64_t(d0) + int64_t(b * hop);
        double er = 0;
        for (size_t i = 0; i < B; i++) {
          double w = 0.5 - 0.5 * std::cos(2 * M_PI * double(i) / double(B));
          float x = R(j0 + int64_t(i) - d0), y = M(j0 + int64_t(i));
          X[i] = w * x;
          Y[i] = w * y;
          er += double(x) * x;
        }
        if (er / double(B) < 1e-8) continue;
        plan.run(X.data(), false);
        plan.run(Y.data(), false);
        double wgt = 1.0;
        if (pass > 0) {
          double e = 0, y2 = 0;
          for (size_t k = 0; k < B / 2; k++) {
            e += std::norm(Y[k] - H[k] * X[k]);
            y2 += std::norm(Y[k]);
          }
          wgt = 1.0 / (e + 1e-4 * y2 + 1e-9);
        }
        for (size_t k = 0; k < B; k++) {
          sxx[k] += wgt * std::norm(X[k]);
          syy[k] += wgt * std::norm(Y[k]);
          sxy[k] += wgt * (Y[k] * std::conj(X[k]));
        }
        used++;
      }
      if (used < 4) return r;
      double mean = 0;
      for (size_t k = 0; k < B; k++) mean += sxx[k];
      mean /= double(B);
      const double lambda = 1e-3 * mean + 1e-30;
      tot = 0;
      res = 0;
      for (size_t k = 0; k < B; k++) {
        H[k] = sxy[k] / (sxx[k] + lambda);
        tot += syy[k];
        res += std::max(0.0, syy[k] - std::norm(sxy[k]) / (sxx[k] + 1e-30));
      }
    }
    std::vector<cd> h = H;
    plan.run(h.data(), true);
    r.path.delay = d0;
    r.path.h.assign(size_t(taps), 0.f);
    const int taper = taps / 4;
    for (int k = 0; k < taps; k++) {
      double w = 1.0;
      if (k >= taps - taper) w = 0.5 + 0.5 * std::cos(M_PI * double(k - (taps - taper)) / double(taper));
      r.path.h[size_t(k)] = float(h[size_t(k)].real() * w);
    }
    r.ceilingDb = 10 * std::log10(tot / (res + 1e-30));
  }
  r.ok = r.peakRatio > 6.0;  // delay found with confidence; the filter itself is harmless if echo is weak
  return r;
}

PathFit fitPathI16(const int16_t* ref, const int16_t* mic, size_t n, int prior, int half, int taps, int pre) {
  return fitPathT(ref, mic, n, 1.f / 32768.f, 1.f / 32768.f, prior, half, taps, pre);
}
PathFit fitPathF(const float* ref, const float* mic, size_t n, int prior, int half, int taps, int pre) {
  return fitPathT(ref, mic, n, 1.f, 1.f, prior, half, taps, pre);
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
