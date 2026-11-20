#include "lab.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace fz {

static std::string fmt(const char* f, double v) {
  char b[64];
  snprintf(b, sizeof b, f, v);
  return b;
}

LatencyStim makeLatencyStim(int sr) {
  LatencyStim s;
  s.chirp = makeChirp(sr, 500, 9000, 0.06, 0.7f);
  const int lead = sr / 2, gap = int(0.6 * sr), K = 8;
  s.samples.assign(size_t(lead + K * gap + sr / 3), 0.f);
  for (int k = 0; k < K; k++) {
    int st = lead + k * gap;
    s.starts.push_back(st);
    for (size_t i = 0; i < s.chirp.size(); i++) s.samples[size_t(st) + i] = s.chirp[i];
  }
  return s;
}

LatencyAnalysis analyzeLatency(const LatencyStim& s, const std::vector<float>& cap, int sr) {
  LatencyAnalysis r;
  std::vector<double> rtl, coh;
  const size_t nc = s.chirp.size();
  for (int st : s.starts) {
    size_t a = size_t(st);
    if (a >= cap.size()) continue;
    size_t b = std::min(cap.size(), a + size_t(0.45 * sr) + nc);
    if (b <= a + nc) continue;
    DelayResult d = findDelay(cap.data() + a, b - a, s.chirp.data(), nc, b - a - nc);
    if (d.lag >= 0 && d.coherence > 0.1 && d.ratio > 6.0) {
      rtl.push_back(double(d.lag));
      coh.push_back(d.coherence);
    }
  }
  const size_t lead = std::min<size_t>(cap.size(), size_t(0.4 * sr));
  double noise = powerDb(cap.data(), lead);
  double pk = 0;
  for (float v : cap) pk = std::max(pk, double(std::fabs(v)));
  std::string j = "{\"test\":\"latency\",\"sent\":" + std::to_string(s.starts.size()) +
                  ",\"valid\":" + std::to_string(rtl.size()) + ",\"rtl_frames\":[";
  for (size_t i = 0; i < rtl.size(); i++) j += (i ? "," : "") + fmt("%.0f", rtl[i]);
  j += "]";
  if (rtl.size() >= 3) {
    std::vector<double> sorted = rtl;
    std::sort(sorted.begin(), sorted.end());
    double med = sorted[sorted.size() / 2], mean = 0, var = 0;
    for (double v : rtl) mean += v;
    mean /= double(rtl.size());
    for (double v : rtl) var += (v - mean) * (v - mean);
    double sd = std::sqrt(var / double(rtl.size()));
    double cm = 0;
    for (double c : coh) cm += c;
    cm /= double(coh.size());
    r.medianFrames = med;
    r.ok = true;
    j += ",\"median_frames\":" + fmt("%.0f", med) + ",\"median_ms\":" + fmt("%.2f", 1000.0 * med / sr) +
         ",\"mean_frames\":" + fmt("%.2f", mean) + ",\"std_frames\":" + fmt("%.2f", sd) +
         ",\"min_frames\":" + fmt("%.0f", sorted.front()) + ",\"max_frames\":" + fmt("%.0f", sorted.back()) +
         ",\"coherence_mean\":" + fmt("%.3f", cm);
  }
  j += ",\"noise_floor_db\":" + fmt("%.1f", noise) + ",\"capture_peak\":" + fmt("%.4f", pk) + "}";
  r.json = j;
  return r;
}

EchoStim makeEchoStim(int sr) {
  EchoStim s;
  s.noiseStart = sr;
  s.noiseLen = 6 * sr;
  s.tail = sr / 2;
  s.samples.assign(size_t(s.noiseStart + s.noiseLen + s.tail), 0.f);
  std::vector<float> nz = makeNoise(size_t(s.noiseLen), 12345u, 0.2f);
  const int fade = sr / 50;
  for (int i = 0; i < s.noiseLen; i++) {
    float g = 1.f;
    if (i < fade) g = float(i) / fade;
    if (i > s.noiseLen - fade) g = float(s.noiseLen - i) / fade;
    s.samples[size_t(s.noiseStart + i)] = nz[size_t(i)] * g;
  }
  return s;
}

EchoAnalysis analyzeEcho(const EchoStim& s, const std::vector<float>& cap, int sr) {
  EchoAnalysis r;
  const size_t N = size_t(s.noiseLen + s.tail);
  if (cap.size() < size_t(s.noiseStart) + N) {
    r.json = "{\"test\":\"echo\",\"ok\":false,\"error\":\"capture too short\"}";
    return r;
  }
  const float* ref = s.samples.data() + s.noiseStart;
  const float* mic = cap.data() + s.noiseStart;
  const size_t trainN = size_t(4 * sr);
  const double floorDb = powerDb(cap.data(), size_t(0.9 * sr));
  const double micDb = powerDb(mic + sr / 2, size_t(s.noiseLen - sr));
  const double refDb = powerDb(ref + sr / 2, size_t(s.noiseLen - sr));

  EchoPath p = estimateEchoPath(ref, mic, trainN, size_t(sr) * 4 / 10, 1024, 32, 3);
  r.path = p;
  std::string j = "{\"test\":\"echo\"";
  j += ",\"noise_floor_db\":" + fmt("%.1f", floorDb) + ",\"mic_db\":" + fmt("%.1f", micDb) +
       ",\"ref_db\":" + fmt("%.1f", refDb) + ",\"snr_db\":" + fmt("%.1f", micDb - floorDb) +
       ",\"echo_return_loss_db\":" + fmt("%.1f", refDb - micDb);
  if (!p.valid() || micDb - floorDb < 6.0) {
    j += ",\"ok\":false,\"error\":\"mic barely heard the playback (volume too low or input muted)\"}";
    r.json = j;
    return r;
  }
  std::vector<float> res(N), resA(N);
  cancelEcho(mic, ref, N, p, res.data(), 0, 0.f);
  cancelEcho(mic, ref, N, p, resA.data(), 128, 0.1f);
  const size_t t0 = size_t(sr / 5), t1 = trainN, u0 = trainN + size_t(sr / 20), u1 = size_t(s.noiseLen);
  double erleTrain = erleDb(mic + t0, res.data() + t0, t1 - t0);
  double erleTest = erleDb(mic + u0, res.data() + u0, u1 - u0);
  double erleTestA = erleDb(mic + u0, resA.data() + u0, u1 - u0);
  double hpk = 0;
  int hk = 0;
  for (size_t k = 0; k < p.h.size(); k++)
    if (std::fabs(p.h[k]) > hpk) {
      hpk = std::fabs(p.h[k]);
      hk = int(k);
    }
  // ---- capture health: things that make ANY canceller fail ----------------------------
  const size_t D = size_t(p.delay + 32);
  size_t clip = 0, zeroFrames = 0, run = 0, regionN = 0;
  for (size_t i = D + size_t(sr / 2); i + size_t(sr / 2) < size_t(s.noiseLen) + D && i < cap.size() - size_t(s.noiseStart); i++) {
    float v = mic[i];
    regionN++;
    if (std::fabs(v) >= 0.985f) clip++;
    if (v == 0.f) run++;
    else {
      if (run >= 8) zeroFrames += run;
      run = 0;
    }
  }
  int jumps = 0, devMax = 0, blocks = 0;
  {
    const size_t blk = size_t(sr / 4);
    for (size_t a = size_t(sr / 2); a + blk + size_t(sr) < size_t(s.noiseLen); a += blk) {
      if (a + D + blk + 300 >= size_t(N)) break;
      DelayResult d = findDelay(mic + a + D - 300, blk + 600, ref + a, blk, 600);
      int dev = d.lag - 300;
      blocks++;
      if (std::abs(dev) > 2) jumps++;
      devMax = std::max(devMax, std::abs(dev));
    }
  }
  r.ok = true;
  j += ",\"clip_pct\":" + fmt("%.3f", 100.0 * double(clip) / double(std::max<size_t>(regionN, 1))) +
       ",\"zero_frames\":" + std::to_string(zeroFrames) + ",\"lag_blocks\":" + std::to_string(blocks) +
       ",\"lag_jumps\":" + std::to_string(jumps) + ",\"lag_dev_max\":" + std::to_string(devMax);
  j += ",\"ok\":true,\"delay_frames\":" + std::to_string(p.delay + 32) +
       ",\"delay_ms\":" + fmt("%.2f", 1000.0 * (p.delay + 32) / sr) +
       ",\"h_peak\":" + fmt("%.4f", hpk) + ",\"h_peak_tap\":" + std::to_string(hk) +
       ",\"erle_train_db\":" + fmt("%.1f", erleTrain) + ",\"erle_test_db\":" + fmt("%.1f", erleTest) +
       ",\"erle_test_adaptive_db\":" + fmt("%.1f", erleTestA) + "}";
  r.json = j;
  return r;
}

}  // namespace fz
