#pragma once
// Full-duplex audio with Oboe. Input has its own callback that feeds a ring buffer
// indexed by an absolute frame counter; the output callback reads the input frames
// at (output frame + K) through fz::InputAligner. Nothing is ever dropped or
// zero-filled to "catch up", so the mic never shifts against the playback mid-take.
#include <oboe/Oboe.h>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "aligner.h"

class Duplex : public oboe::AudioStreamDataCallback, public oboe::AudioStreamErrorCallback {
 public:
  struct Processor {
    virtual ~Processor() = default;
    virtual void process(const float* in, float* out, int n) = 0;
  };
  struct Config {
    int sampleRate = 48000;
    int inputPreset = 9;  // oboe::InputPreset: 1 generic, 5 camcorder, 6 voice rec, 7 voice comm, 9 unprocessed
    int perfMode = 10;    // oboe::PerformanceMode: 10 none, 12 low latency
  };

  Duplex() : inCb_(this) {}
  ~Duplex() override { close(); }
  bool open(const Config& cfg, Processor* proc);
  bool start();
  void stop();
  void close();
  bool isOpen() const { return out_ != nullptr; }
  int64_t lateFrames() const { return aligner_.lateFrames(); }
  int resyncs() const { return aligner_.resyncs(); }
  std::string infoJson() const;

  oboe::DataCallbackResult onAudioReady(oboe::AudioStream* s, void* data, int32_t n) override;
  void onErrorAfterClose(oboe::AudioStream* s, oboe::Result r) override;

 private:
  struct InputCb : oboe::AudioStreamDataCallback {
    explicit InputCb(Duplex* d) : d_(d) {}
    oboe::DataCallbackResult onAudioReady(oboe::AudioStream*, void* data, int32_t n) override {
      d_->aligner_.write(static_cast<const float*>(data), n);
      return oboe::DataCallbackResult::Continue;
    }
    Duplex* d_;
  };

  Config cfg_;
  Processor* proc_ = nullptr;
  std::shared_ptr<oboe::AudioStream> in_, out_;
  InputCb inCb_;
  fz::InputAligner aligner_;
  std::vector<float> inBuf_;
  std::atomic<bool> restarting_{false};
  std::atomic<bool> running_{false};
  bool inputFellBack_ = false;
  int64_t cushion_ = 0;
};
