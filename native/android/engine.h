#pragma once
#include <string>
#include <vector>

#include "duplex.h"
#include "studio.h"

class Engine : public Duplex::Processor {
 public:
  fz::Studio studio;
  Duplex duplex;
  Duplex::Config cfg;
  std::string dir;

  void process(const float* in, float* out, int n) override { studio.process(in, out, n); }

  bool init(const std::string& projectDir);
  bool startStreams();
  void stopStreams();

  // Lab (each opens its own temporary duplex stream; studio streams are stopped first).
  std::string labLatency(int preset, int perf, const std::string& dumpDir, const std::string& tag);
  std::string labEcho(int preset, int perf, const std::string& dumpDir, const std::string& tag,
                      bool adoptAsCalibration);
  // latency + echo with the studio's current config; stores both.
  std::string calibrate(const std::string& dumpDir);
  bool echoValid() const { return studio.echoPath().valid(); }

 private:
  struct Capture {
    bool ok = false;
    std::vector<float> cap;
    std::string streamJson;
  };
  Capture runStimulus(const Duplex::Config& c, const std::vector<float>& stim, size_t extraTail);
};
