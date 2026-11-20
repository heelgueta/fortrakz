#pragma once
// Pure C++17 DSP used by both the Android app and the host unit tests.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fz {

// ---- signal generators -----------------------------------------------------
std::vector<float> makeChirp(int sr, double f0, double f1, double seconds, float amp);
std::vector<float> makeNoise(size_t n, uint32_t seed, float rms);

// ---- correlation / delay ---------------------------------------------------
// c[k] = sum_j a[j+k] * b[j] for k in [0, maxLag]
std::vector<double> xcorrPositive(const float* a, size_t na, const float* b, size_t nb, size_t maxLag);

struct DelayResult {
  int lag = -1;          // sample offset in `sig` where `ref` best aligns
  double coherence = 0;  // |c| / sqrt(E_ref * E_window), 0..1
  double ratio = 0;      // peak / mean |c| over the searched lags
};
DelayResult findDelay(const float* sig, size_t ns, const float* ref, size_t nr, size_t maxLag);

// ---- filtering ---------------------------------------------------------------
// y[j] = sum_{k<L} h[k] * x[j-k]   (causal, same length as x)
void convolveCausal(const float* x, size_t n, const float* h, size_t L, float* y);

// ---- echo path (speaker -> mic) ---------------------------------------------
struct EchoPath {
  int delay = 0;  // reference is delayed by this many samples before filtering
  std::vector<float> h;
  bool valid() const { return !h.empty(); }
};

// Estimate with a white-noise reference. `pre` samples of headroom before the
// main peak are kept so small timing jitter stays inside the filter.
EchoPath estimateEchoPath(const float* ref, const float* mic, size_t n, size_t maxDelay,
                          int taps, int pre, int iters);

// out = mic - echo_estimate. If adaptTaps > 0 a guarded NLMS stage tracks small
// changes of the path (frozen whenever the residual is louder than the echo
// estimate, i.e. while someone is singing).
void cancelEcho(const float* mic, const float* ref, size_t n, const EchoPath& p, float* out,
                int adaptTaps, float mu);

double powerDb(const float* x, size_t n);
// 10*log10(P(mic)/P(res)) over n samples
double erleDb(const float* mic, const float* res, size_t n);

bool saveEchoPath(const std::string& path, const EchoPath& p);
bool loadEchoPath(const std::string& path, EchoPath& p);

// ---- wav ---------------------------------------------------------------------
bool writeWavMono16(const std::string& path, const int16_t* data, size_t n, int sr);
bool writeWavMonoFloat(const std::string& path, const float* data, size_t n, int sr);

inline int16_t toI16(float v) {
  v *= 32768.0f;
  if (v > 32767.0f) v = 32767.0f;
  if (v < -32768.0f) v = -32768.0f;
  return int16_t(v);
}

}  // namespace fz
