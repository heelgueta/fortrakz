#include "studio.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace fz {

static inline float softClip(float x) {
  const float t = 0.85f;
  float a = std::fabs(x);
  if (a <= t) return x;
  float y = t + (1.f - t) * std::tanh((a - t) / (1.f - t));
  return x < 0 ? -y : y;
}

Studio::Studio() {
  for (int t = 0; t < kTracks; t++) {
    data_[t] = static_cast<int16_t*>(calloc(size_t(kCap), sizeof(int16_t)));
    len_[t] = 0;
    gain_[t] = 0.8f;
    mute_[t] = false;
    solo_[t] = false;
    peak_[t] = 0.f;
  }
  raw_ = static_cast<int16_t*>(calloc(size_t(kCap), sizeof(int16_t)));
  ref_ = static_cast<int16_t*>(calloc(size_t(kCap), sizeof(int16_t)));
}

Studio::~Studio() {
  for (auto* d : data_) free(d);
  free(raw_);
  free(ref_);
}

int64_t Studio::maxLen() const {
  int64_t m = 0;
  for (int t = 0; t < kTracks; t++) m = std::max(m, len_[t].load());
  return m;
}

// ---- audio thread ----------------------------------------------------------------

void Studio::process(const float* in, float* out, int n) {
  float ip = 0.f;
  for (int i = 0; i < n; i++) ip = std::max(ip, std::fabs(in[i]));
  inPeak_.store(std::max(ip, inPeak_.load() * 0.92f));

  const int st = state_.load(std::memory_order_acquire);
  if (st == kStopped) {
    std::memset(out, 0, size_t(n) * sizeof(float));
    for (int t = 0; t < kTracks; t++) peak_[t].store(peak_[t].load() * 0.8f);
    return;
  }
  const bool rec = (st == kRecording);
  const int skip = rec ? armed_.load() : -1;
  const int64_t p = pos_.load();

  int64_t len[kTracks];
  float gain[kTracks];
  bool audible[kTracks];
  bool anySolo = false;
  for (int t = 0; t < kTracks; t++) anySolo |= solo_[t].load();
  for (int t = 0; t < kTracks; t++) {
    len[t] = len_[t].load();
    gain[t] = gain_[t].load();
    audible[t] = (t != skip) && !mute_[t].load() && (!anySolo || solo_[t].load());
  }

  float tp[kTracks] = {0, 0, 0, 0};
  for (int i = 0; i < n; i++) {
    const int64_t q = p + i;
    float s = 0.f;
    for (int t = 0; t < kTracks; t++) {
      if (q >= len[t]) continue;
      float v = float(data_[t][q]) * (1.f / 32768.f) * gain[t];
      tp[t] = std::max(tp[t], std::fabs(v));
      if (audible[t]) s += v;
    }
    out[i] = softClip(s);
  }
  for (int t = 0; t < kTracks; t++) {
    float v = (rec && t == skip) ? ip : tp[t];
    peak_[t].store(std::max(v, peak_[t].load() * 0.9f));
  }

  if (rec) {
    int64_t j = recN_.load(std::memory_order_relaxed);
    for (int i = 0; i < n && j + i < kCap; i++) {
      raw_[j + i] = toI16(in[i]);
      ref_[j + i] = toI16(out[i]);
    }
    j = std::min<int64_t>(j + n, kCap);
    recN_.store(j, std::memory_order_release);
    if (j >= kCap) state_.store(kStopped);
  }
  const int64_t np = p + n;
  pos_.store(np);
  if (!rec && np >= maxLen() + kSr / 2) state_.store(kStopped);
}

// ---- transport ----------------------------------------------------------------------

void Studio::play() {
  std::lock_guard<std::mutex> g(ctl_);
  if (state_.load() == kStopped) {
    if (pos_.load() >= maxLen()) pos_.store(0);
    state_.store(kPlaying, std::memory_order_release);
  }
}

bool Studio::stop() {
  std::lock_guard<std::mutex> g(ctl_);
  state_.exchange(kStopped);
  // Let an in-flight audio callback finish touching the take buffers.
  std::this_thread::sleep_for(std::chrono::milliseconds(40));
  return recTrack_ >= 0 && recN_.load() > 0;  // a take is waiting for finalizeTake()
}

bool Studio::record(int track) {
  std::lock_guard<std::mutex> g(ctl_);
  if (track < 0 || track >= kTracks) return false;
  state_.store(kStopped);
  std::this_thread::sleep_for(std::chrono::milliseconds(40));
  recStart_ = pos_.load();
  recTrack_ = track;
  recN_.store(0);
  armed_.store(track);
  state_.store(kRecording, std::memory_order_release);
  return true;
}

void Studio::seek(int64_t f) {
  if (f < 0) f = 0;
  if (state_.load() == kRecording) return;
  pos_.store(f);
}

void Studio::setTrack(int t, float gain, bool mute, bool solo) {
  if (t < 0 || t >= kTracks) return;
  gain_[t].store(gain);
  mute_[t].store(mute);
  solo_[t].store(solo);
}

// ---- take processing -------------------------------------------------------------------

std::string Studio::place(int t, const int16_t* raw, const int16_t* ref, int64_t n, int64_t start,
                          int64_t lat, bool wipePrevious) {
  Take& tk = take_[t];
  if (wipePrevious && tk.has && tk.pEnd > tk.pStart)
    std::memset(data_[t] + tk.pStart, 0, size_t(tk.pEnd - tk.pStart) * sizeof(int16_t));

  const bool clean = cleanMode_ != kCleanOff && echo_.valid();
  const int adapt = (clean && cleanMode_ == kCleanAdaptive) ? 128 : 0;
  const int64_t chunk = int64_t(60) * kSr;
  const int64_t pad = clean ? int64_t(echo_.delay) + int64_t(echo_.h.size()) + (adapt ? 2 * kSr : 0) : 0;
  double eIn = 0, eOut = 0;
  int16_t peak = 0;

  for (int64_t a = 0; a < n; a += chunk) {
    const int64_t b = std::min(n, a + chunk);
    const int64_t w0 = std::max<int64_t>(0, a - pad);
    const size_t len = size_t(b - w0);
    std::vector<float> mic(len), rf(len), out(len);
    for (size_t i = 0; i < len; i++) {
      mic[i] = float(raw[w0 + int64_t(i)]) * (1.f / 32768.f);
      rf[i] = float(ref[w0 + int64_t(i)]) * (1.f / 32768.f);
    }
    if (clean)
      cancelEcho(mic.data(), rf.data(), len, echo_, out.data(), adapt, 0.1f);
    else
      out = mic;
    for (int64_t j = a; j < b; j++) {
      const int64_t idx = start + j - lat;
      const float o = out[size_t(j - w0)], m = mic[size_t(j - w0)];
      eIn += double(m) * m;
      eOut += double(o) * o;
      if (idx < 0 || idx >= kCap) continue;
      int16_t v = toI16(o);
      data_[t][idx] = v;
      peak = std::max<int16_t>(peak, int16_t(std::abs(int(v))));
    }
  }
  tk.has = true;
  tk.start = start;
  tk.n = n;
  tk.lat = lat;
  tk.pStart = std::max<int64_t>(0, start - lat);
  tk.pEnd = std::min<int64_t>(kCap, start + n - lat);
  if (tk.pEnd > len_[t].load()) len_[t].store(tk.pEnd);

  double inDb = 10 * std::log10(eIn / double(std::max<int64_t>(n, 1)) + 1e-20);
  double outDb = 10 * std::log10(eOut / double(std::max<int64_t>(n, 1)) + 1e-20);
  char buf[512];
  snprintf(buf, sizeof buf,
           "{\"ok\":true,\"track\":%d,\"seconds\":%.2f,\"latency_frames\":%lld,\"cleaned\":%s,"
           "\"adaptive\":%s,\"mic_db\":%.1f,\"result_db\":%.1f,\"reduction_db\":%.1f,\"peak\":%.3f}",
           t, double(n) / kSr, (long long)lat, clean ? "true" : "false", adapt ? "true" : "false", inDb,
           outDb, inDb - outDb, double(peak) / 32768.0);
  return buf;
}

std::string Studio::finalizeTake() {
  std::lock_guard<std::mutex> g(ctl_);
  const int64_t n = std::min<int64_t>(recN_.load(std::memory_order_acquire), kCap);
  const int t = recTrack_;
  if (t < 0 || n <= 0) return "{\"ok\":false,\"error\":\"no take\"}";
  saveTakeFiles(t, raw_, ref_, n);
  std::string r = place(t, raw_, ref_, n, recStart_, latency_, false);
  recTrack_ = -1;
  recN_.store(0);
  pos_.store(recStart_);
  save();
  return r;
}

std::string Studio::reclean(int t) {
  std::lock_guard<std::mutex> g(ctl_);
  if (t < 0 || t >= kTracks || !take_[t].has || dir_.empty()) return "{\"ok\":false,\"error\":\"no stored take\"}";
  const Take tk = take_[t];
  std::vector<int16_t> raw(size_t(tk.n)), ref(size_t(tk.n));
  char p1[512], p2[512];
  snprintf(p1, sizeof p1, "%s/take%d_raw.pcm", dir_.c_str(), t);
  snprintf(p2, sizeof p2, "%s/take%d_ref.pcm", dir_.c_str(), t);
  FILE* f1 = fopen(p1, "rb");
  FILE* f2 = fopen(p2, "rb");
  bool ok = f1 && f2 && fread(raw.data(), 2, raw.size(), f1) == raw.size() &&
            fread(ref.data(), 2, ref.size(), f2) == ref.size();
  if (f1) fclose(f1);
  if (f2) fclose(f2);
  if (!ok) return "{\"ok\":false,\"error\":\"take files missing\"}";
  std::string r = place(t, raw.data(), ref.data(), tk.n, tk.start, latency_, true);
  save();
  return r;
}

void Studio::clearTrack(int t) {
  std::lock_guard<std::mutex> g(ctl_);
  if (t < 0 || t >= kTracks) return;
  std::memset(data_[t], 0, size_t(std::min<int64_t>(len_[t].load(), kCap)) * sizeof(int16_t));
  len_[t].store(0);
  take_[t] = Take();
  save();
}

void Studio::clearAll() {
  for (int t = 0; t < kTracks; t++) clearTrack(t);
  pos_.store(0);
}

// ---- persistence ------------------------------------------------------------------------

bool Studio::saveTakeFiles(int t, const int16_t* raw, const int16_t* ref, int64_t n) {
  if (dir_.empty()) return false;
  char p1[512], p2[512];
  snprintf(p1, sizeof p1, "%s/take%d_raw.pcm", dir_.c_str(), t);
  snprintf(p2, sizeof p2, "%s/take%d_ref.pcm", dir_.c_str(), t);
  FILE* f1 = fopen(p1, "wb");
  FILE* f2 = fopen(p2, "wb");
  bool ok = f1 && f2;
  if (f1) {
    fwrite(raw, 2, size_t(n), f1);
    fclose(f1);
  }
  if (f2) {
    fwrite(ref, 2, size_t(n), f2);
    fclose(f2);
  }
  return ok;
}

bool Studio::save() {
  if (dir_.empty()) return false;
  char path[512];
  for (int t = 0; t < kTracks; t++) {
    snprintf(path, sizeof path, "%s/track%d.pcm", dir_.c_str(), t);
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    fwrite(data_[t], 2, size_t(len_[t].load()), f);
    fclose(f);
  }
  snprintf(path, sizeof path, "%s/project.txt", dir_.c_str());
  FILE* f = fopen(path, "w");
  if (!f) return false;
  for (int t = 0; t < kTracks; t++) {
    const Take& k = take_[t];
    fprintf(f, "%d %lld %.4f %d %d %d %lld %lld %lld %lld %lld\n", t, (long long)len_[t].load(),
            gain_[t].load(), mute_[t].load() ? 1 : 0, solo_[t].load() ? 1 : 0, k.has ? 1 : 0,
            (long long)k.start, (long long)k.n, (long long)k.lat, (long long)k.pStart,
            (long long)k.pEnd);
  }
  fclose(f);
  return true;
}

bool Studio::load() {
  if (dir_.empty()) return false;
  char path[512];
  snprintf(path, sizeof path, "%s/project.txt", dir_.c_str());
  FILE* f = fopen(path, "r");
  if (!f) return false;
  int t, mute, solo, has;
  long long len, start, n, lat, ps, pe;
  float gain;
  while (fscanf(f, "%d %lld %f %d %d %d %lld %lld %lld %lld %lld", &t, &len, &gain, &mute, &solo,
                &has, &start, &n, &lat, &ps, &pe) == 11) {
    if (t < 0 || t >= kTracks) continue;
    len = std::max<long long>(0, std::min<long long>(len, kCap));
    snprintf(path, sizeof path, "%s/track%d.pcm", dir_.c_str(), t);
    FILE* d = fopen(path, "rb");
    if (d) {
      size_t got = fread(data_[t], 2, size_t(len), d);
      fclose(d);
      len = (long long)got;
    } else {
      len = 0;
    }
    len_[t].store(len);
    gain_[t].store(gain);
    mute_[t].store(mute != 0);
    solo_[t].store(solo != 0);
    take_[t] = Take{has != 0, start, n, lat, ps, pe};
  }
  fclose(f);
  pos_.store(0);
  return true;
}

bool Studio::exportMix(const std::string& wavPath) {
  const int64_t len = maxLen();
  if (len <= 0) return false;
  bool anySolo = false;
  for (int t = 0; t < kTracks; t++) anySolo |= solo_[t].load();
  std::vector<int16_t> out(static_cast<size_t>(len), 0);
  for (int64_t q = 0; q < len; q++) out[size_t(q)] = toI16(mixAt(q, -1, anySolo));
  return writeWavMono16(wavPath, out.data(), out.size(), kSr);
}

float Studio::mixAt(int64_t q, int skip, bool anySolo) const {
  float s = 0.f;
  for (int t = 0; t < kTracks; t++) {
    if (t == skip || q >= len_[t].load()) continue;
    if (mute_[t].load() || (anySolo && !solo_[t].load())) continue;
    s += float(data_[t][q]) * (1.f / 32768.f) * gain_[t].load();
  }
  return softClip(s);
}

void Studio::status(float* out, int cap) const {
  float v[10];
  v[0] = float(pos_.load()) / kSr;
  v[1] = float(maxLen()) / kSr;
  v[2] = float(state_.load());
  for (int t = 0; t < kTracks; t++) v[3 + t] = peak_[t].load();
  v[7] = inPeak_.load();
  v[8] = float(recN_.load()) / kSr;
  v[9] = float(armed_.load());
  for (int i = 0; i < cap && i < 10; i++) out[i] = v[i];
}

}  // namespace fz
