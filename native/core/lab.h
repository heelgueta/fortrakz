#pragma once
// Analysis half of the lab: stimulus generation and the maths that turns a
// captured microphone signal into latency / echo-path / ERLE numbers.
// Pure C++ so it can be unit tested against simulated acoustics on the host.
#include <string>
#include <vector>

#include "dsp.h"

namespace fz {

struct LatencyStim {
  std::vector<float> samples;
  std::vector<int> starts;  // output frame where each chirp begins
  std::vector<float> chirp;
};
LatencyStim makeLatencyStim(int sr);

struct LatencyAnalysis {
  std::string json;
  double medianFrames = -1;
  bool ok = false;
};
LatencyAnalysis analyzeLatency(const LatencyStim& s, const std::vector<float>& cap, int sr);

struct EchoStim {
  std::vector<float> samples;
  int noiseStart = 0, noiseLen = 0, tail = 0;
};
EchoStim makeEchoStim(int sr);

struct EchoAnalysis {
  std::string json;
  EchoPath path;
  bool ok = false;
};
EchoAnalysis analyzeEcho(const EchoStim& s, const std::vector<float>& cap, int sr);

}  // namespace fz
