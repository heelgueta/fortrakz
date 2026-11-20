#pragma once
// Full-duplex audio with Oboe: one output stream drives the callback, the input
// stream is polled (non-blocking) inside it, so input and output share a clock.
#include <oboe/Oboe.h>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

class Duplex : public oboe::AudioStreamDataCallback, public oboe::AudioStreamErrorCallback {
 public:
  struct Processor {
    virtual ~Processor() = default;
    virtual void process(const float* in, float* out, int n) = 0;
  };
  struct Config {
    int sampleRate = 48000;
    int inputPreset = 9;  // oboe::InputPreset: 1 generic, 5 camcorder, 6 voice rec, 7 voice comm, 9 unprocessed
    int perfMode = 12;    // oboe::PerformanceMode: 10 none, 12 low latency
  };

  ~Duplex() override { close(); }
  bool open(const Config& cfg, Processor* proc);
  bool start();
  void stop();
  void close();
  bool isOpen() const { return out_ != nullptr; }
  int xruns() const { return xruns_.load(); }
  std::string infoJson() const;

  oboe::DataCallbackResult onAudioReady(oboe::AudioStream* s, void* data, int32_t n) override;
  void onErrorAfterClose(oboe::AudioStream* s, oboe::Result r) override;

 private:
  Config cfg_;
  Processor* proc_ = nullptr;
  std::shared_ptr<oboe::AudioStream> in_, out_;
  std::vector<float> inBuf_;
  std::atomic<int> xruns_{0};
  std::atomic<int> callbacks_{0};
  std::atomic<bool> restarting_{false};
  std::atomic<bool> running_{false};
  bool inputFellBack_ = false;
  int32_t cushion_ = 0;
};
