#include "engine.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

#include "lab.h"

namespace {

struct StimProc : Duplex::Processor {
  const std::vector<float>* stim = nullptr;
  std::vector<float> cap;
  std::atomic<size_t> idx{0};
  std::atomic<bool> done{false};
  void process(const float* in, float* out, int n) override {
    size_t base = idx.load();
    for (int i = 0; i < n; i++) {
      size_t k = base + size_t(i);
      out[i] = k < stim->size() ? (*stim)[k] : 0.f;
      if (k < cap.size()) cap[k] = in[i];
    }
    base += size_t(n);
    idx.store(base);
    if (base >= cap.size()) done.store(true);
  }
};

std::string wrap(const Duplex::Config& c, const std::string& tag, const std::string& stream,
                 const std::string& result) {
  char b[256];
  snprintf(b, sizeof b, "{\"tag\":\"%s\",\"preset\":%d,\"perf\":%d,\"sample_rate\":%d,", tag.c_str(),
           c.inputPreset, c.perfMode, c.sampleRate);
  return std::string(b) + "\"stream\":" + stream + ",\"result\":" + result + "}";
}

}  // namespace

bool Engine::init(const std::string& projectDir) {
  dir = projectDir;
  studio.setProjectDir(dir);
  studio.load();
  fz::EchoPath p;
  if (fz::loadEchoPath(dir + "/echo2.bin", p)) studio.setEchoPath(p);
  return studio.echoPath().valid();
}

bool Engine::startStreams() {
  if (duplex.isOpen()) return true;
  if (!duplex.open(cfg, this)) return false;
  return duplex.start();
}

void Engine::stopStreams() {
  studio.stop();
  duplex.close();
}

Engine::Capture Engine::runStimulus(const Duplex::Config& c, const std::vector<float>& stim,
                                    size_t extraTail) {
  Capture r;
  stopStreams();
  StimProc sp;
  sp.stim = &stim;
  sp.cap.assign(stim.size() + extraTail, 0.f);
  Duplex d;
  if (!d.open(c, &sp)) return r;
  if (!d.start()) {
    d.close();
    return r;
  }
  const double secs = double(sp.cap.size()) / c.sampleRate;
  for (int i = 0; i < int((secs + 4.0) * 100) && !sp.done.load(); i++)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  r.streamJson = d.infoJson();
  d.close();
  r.ok = sp.done.load();
  r.cap = std::move(sp.cap);
  return r;
}

static std::string failJson(const char* what) {
  return std::string("{\"ok\":false,\"error\":\"") + what + "\"}";
}

std::string Engine::labLatency(int preset, int perf, const std::string& dumpDir, const std::string& tag) {
  Duplex::Config c = cfg;
  c.inputPreset = preset;
  c.perfMode = perf;
  fz::LatencyStim s = fz::makeLatencyStim(c.sampleRate);
  Capture cap = runStimulus(c, s.samples, size_t(c.sampleRate / 2));
  if (!cap.ok) return wrap(c, tag, "{}", failJson("could not open or run audio streams"));
  fz::LatencyAnalysis a = fz::analyzeLatency(s, cap.cap, c.sampleRate);
  if (!dumpDir.empty()) fz::writeWavMonoFloat(dumpDir + "/" + tag + "_lat_cap.wav", cap.cap.data(), cap.cap.size(), c.sampleRate);
  return wrap(c, tag, cap.streamJson, a.json);
}

std::string Engine::labEcho(int preset, int perf, const std::string& dumpDir, const std::string& tag,
                            bool adopt) {
  Duplex::Config c = cfg;
  c.inputPreset = preset;
  c.perfMode = perf;
  fz::EchoStim s = fz::makeEchoStim(c.sampleRate);
  Capture cap = runStimulus(c, s.samples, size_t(c.sampleRate));
  if (!cap.ok) return wrap(c, tag, "{}", failJson("could not open or run audio streams"));
  fz::EchoAnalysis a = fz::analyzeEcho(s, cap.cap, c.sampleRate);
  if (!dumpDir.empty()) {
    fz::writeWavMonoFloat(dumpDir + "/" + tag + "_echo_stim.wav", s.samples.data(), s.samples.size(), c.sampleRate);
    fz::writeWavMonoFloat(dumpDir + "/" + tag + "_echo_cap.wav", cap.cap.data(), cap.cap.size(), c.sampleRate);
  }
  if (adopt && a.ok) {
    studio.setEchoPath(a.path);
    fz::saveEchoPath(dir + "/echo2.bin", a.path);
  }
  return wrap(c, tag, cap.streamJson, a.json);
}

std::string Engine::calibrate(const std::string& dumpDir) {
  // Try the chosen mic/latency mode first; some phones return silence in low-latency mode
  // (seen on a Galaxy S23: mic dead in 3 of 5 modes). Fall back to modes that actually hear us.
  struct Cand { int preset, perf; };
  std::vector<Cand> cands = {{cfg.inputPreset, cfg.perfMode}, {cfg.inputPreset, 10}, {1, 10}, {5, 10}};
  std::string lat;
  double med = -1;
  int usedPreset = cfg.inputPreset, usedPerf = cfg.perfMode;
  std::vector<Cand> tried;
  for (const Cand& c : cands) {
    bool dup = false;
    for (const Cand& t : tried) dup |= (t.preset == c.preset && t.perf == c.perf);
    if (dup) continue;
    tried.push_back(c);
    lat = labLatency(c.preset, c.perf, dumpDir, "calib");
    med = -1;
    size_t p = lat.find("\"median_frames\":");
    if (p != std::string::npos) med = atof(lat.c_str() + p + 16);
    if (med > 0) {
      usedPreset = c.preset;
      usedPerf = c.perf;
      break;
    }
  }
  cfg.inputPreset = usedPreset;
  cfg.perfMode = usedPerf;
  std::string echo = med > 0 ? labEcho(usedPreset, usedPerf, dumpDir, "calib", true) : "{}";
  bool echoOk = echo.find("\"ok\":true") != std::string::npos;
  char b[220];
  snprintf(b, sizeof b,
           "{\"ok\":%s,\"rtl_frames\":%.0f,\"echo_ok\":%s,\"preset_used\":%d,\"perf_used\":%d,\"attempts\":%d,\"latency\":",
           med > 0 ? "true" : "false", med, echoOk ? "true" : "false", usedPreset, usedPerf, int(tried.size()));
  return std::string(b) + lat + ",\"echo\":" + echo + "}";
}
