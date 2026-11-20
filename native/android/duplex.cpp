#include "duplex.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <thread>

#include <android/log.h>
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "fortrakz", __VA_ARGS__)

static constexpr int32_t kMaxBlock = 16384;

bool Duplex::open(const Config& cfg, Processor* proc) {
  close();
  cfg_ = cfg;
  proc_ = proc;
  inputFellBack_ = false;
  inBuf_.assign(kMaxBlock, 0.f);

  auto build = [&](oboe::Direction dir, int preset) {
    oboe::AudioStreamBuilder b;
    b.setDirection(dir)
        ->setPerformanceMode(static_cast<oboe::PerformanceMode>(cfg.perfMode))
        ->setSharingMode(oboe::SharingMode::Exclusive)
        ->setFormat(oboe::AudioFormat::Float)
        ->setFormatConversionAllowed(true)
        ->setChannelCount(1)
        ->setChannelConversionAllowed(true)
        ->setSampleRate(cfg.sampleRate)
        ->setSampleRateConversionQuality(oboe::SampleRateConversionQuality::Medium)
        ->setErrorCallback(this);
    if (dir == oboe::Direction::Input) {
      b.setInputPreset(static_cast<oboe::InputPreset>(preset))->setDataCallback(&inCb_);
    } else {
      b.setUsage(oboe::Usage::Media)->setContentType(oboe::ContentType::Music)->setDataCallback(this);
    }
    std::shared_ptr<oboe::AudioStream> s;
    oboe::Result r = b.openStream(s);
    if (r != oboe::Result::OK) LOGI("open %d failed: %s", int(dir), oboe::convertToText(r));
    return r == oboe::Result::OK ? s : nullptr;
  };

  in_ = build(oboe::Direction::Input, cfg.inputPreset);
  if (!in_ && cfg.inputPreset != 1) {  // device refused this preset: fall back to generic
    inputFellBack_ = true;
    in_ = build(oboe::Direction::Input, 1);
  }
  if (!in_) return false;
  out_ = build(oboe::Direction::Output, 0);
  if (!out_) {
    in_->close();
    in_.reset();
    return false;
  }
  out_->setBufferSizeInFrames(out_->getFramesPerBurst() * 2);
  cushion_ = int64_t(in_->getFramesPerBurst()) + out_->getFramesPerBurst();
  return true;
}

bool Duplex::start() {
  if (!in_ || !out_) return false;
  aligner_.reset();
  if (in_->requestStart() != oboe::Result::OK) return false;
  // let the input deliver a few bursts so the first output callback anchors on real data
  for (int i = 0; i < 160; i++) {
    if (aligner_.written() >= 3 * int64_t(in_->getFramesPerBurst())) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  running_ = true;
  return out_->requestStart() == oboe::Result::OK;
}

void Duplex::stop() {
  running_ = false;
  if (out_) out_->requestStop();
  if (in_) in_->requestStop();
}

void Duplex::close() {
  running_ = false;
  if (out_) {
    out_->requestStop();
    out_->close();
    out_.reset();
  }
  if (in_) {
    in_->requestStop();
    in_->close();
    in_.reset();
  }
}

oboe::DataCallbackResult Duplex::onAudioReady(oboe::AudioStream*, void* data, int32_t n) {
  float* out = static_cast<float*>(data);
  if (!running_.load() || n > kMaxBlock) {
    std::fill(out, out + n, 0.f);
    return oboe::DataCallbackResult::Continue;
  }
  aligner_.read(inBuf_.data(), n, cushion_);
  proc_->process(inBuf_.data(), out, n);
  return oboe::DataCallbackResult::Continue;
}

void Duplex::onErrorAfterClose(oboe::AudioStream*, oboe::Result r) {
  LOGI("stream error: %s", oboe::convertToText(r));
  if (!running_.load() || restarting_.exchange(true)) return;
  std::thread([this] {
    Config c = cfg_;
    Processor* p = proc_;
    close();
    if (open(c, p)) start();
    restarting_ = false;
  }).detach();
}

std::string Duplex::infoJson() const {
  char b[1000];
  if (!in_ || !out_) return "{}";
  double lat = -1;
  if (auto l = out_->calculateLatencyMillis()) lat = l.value();
  auto xr = out_->getXRunCount();
  snprintf(b, sizeof b,
           "{\"engine\":\"ring-aligned\",\"api\":\"%s\",\"sample_rate\":%d,\"in_sample_rate\":%d,\"in_preset_req\":%d,"
           "\"in_preset_actual\":%d,\"in_fallback\":%s,\"perf_req\":%d,\"in_perf\":%d,\"out_perf\":%d,"
           "\"in_sharing\":%d,\"out_sharing\":%d,\"in_burst\":%d,\"out_burst\":%d,\"out_buffer\":%d,"
           "\"out_capacity\":%d,\"in_capacity\":%d,\"in_device\":%d,\"out_device\":%d,"
           "\"reported_out_latency_ms\":%.2f,\"xruns\":%d,\"late_frames\":%lld,\"resyncs\":%d,"
           "\"input_offset\":%lld}",
           oboe::convertToText(out_->getAudioApi()), out_->getSampleRate(), in_->getSampleRate(),
           cfg_.inputPreset, int(in_->getInputPreset()), inputFellBack_ ? "true" : "false", cfg_.perfMode,
           int(in_->getPerformanceMode()), int(out_->getPerformanceMode()), int(in_->getSharingMode()),
           int(out_->getSharingMode()), in_->getFramesPerBurst(), out_->getFramesPerBurst(),
           out_->getBufferSizeInFrames(), out_->getBufferCapacityInFrames(), in_->getBufferCapacityInFrames(),
           in_->getDeviceId(), out_->getDeviceId(), lat, xr ? xr.value() : -1,
           (long long)aligner_.lateFrames(), aligner_.resyncs(), (long long)aligner_.offset());
  return b;
}
