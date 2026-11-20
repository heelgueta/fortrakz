#pragma once
// The 4-track engine, independent of Android: transport, mixing, take capture,
// latency-compensated placement, echo cancellation of the speaker bleed and
// project persistence. The Android layer only feeds process() from Oboe.
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "dsp.h"

namespace fz {

class Studio {
 public:
  static constexpr int kTracks = 4;
  static constexpr int kSr = 48000;
  static constexpr int64_t kCap = int64_t(600) * kSr;  // 10 min per track
  enum State { kStopped = 0, kPlaying = 1, kRecording = 2, kCountIn = 3 };
  // 0 off | 1 calibrated filter | 2 calibrated + adaptive | 3 learn from the take | 4 learn + adaptive
  enum CleanMode { kCleanOff = 0, kCleanFixed = 1, kCleanAdaptive = 2, kCleanLearn = 3, kCleanLearnAdaptive = 4 };

  Studio();
  ~Studio();
  Studio(const Studio&) = delete;
  Studio& operator=(const Studio&) = delete;

  // ---- audio thread (no allocation, no locks) ----
  void process(const float* in, float* out, int n);

  // ---- control thread ----
  void play();
  // Returns true if a recording was in progress (call finalizeTake() next).
  bool stop();
  bool record(int track);
  void seek(int64_t frame);
  int state() const { return state_.load(); }
  int64_t position() const { return pos_.load(); }

  void setTrack(int t, float gain, bool mute, bool solo);
  void setLatencyFrames(int f) { latency_ = f; }  // calibrated round trip
  void setTrimFrames(int f) { trim_ = f; }        // user fine-tune, added to every placement
  int latencyFrames() const { return latency_; }
  // metronome: bpm 30..300, count-in beats 0..8 (before play/record from stop),
  // clickPlay = keep clicking during playback/recording; countOnPlay = also count in before plain playback
  void setClick(int bpm, int countIn, bool clickPlay, bool countOnPlay);
  void setCleanMode(int m) { cleanMode_ = m; }
  void setEchoPath(const EchoPath& p) { echo_ = p; }
  const EchoPath& echoPath() const { return echo_; }

  // Process the take that just ended and write it into its track. Returns JSON.
  std::string finalizeTake();
  // Re-run cleaning/placement of a stored take with the current settings.
  std::string reclean(int t);
  void clearTrack(int t);
  void clearAll();
  int64_t trackLength(int t) const { return len_[t].load(); }
  bool hasTake(int t) const { return take_[t].has; }
  // max-abs per `bucket` frames, scaled 0..1. Returns bucket count written.
  int peaks(int t, int bucket, float* out, int cap) const;

  void setProjectDir(const std::string& dir) { dir_ = dir; }
  bool save();
  bool load();
  bool exportMix(const std::string& wavPath);

  // out: [0]=pos s [1]=len s [2]=state [3..6]=track peaks [7]=input peak
  //      [8]=take seconds so far [9]=armed track [10]=count-in beat (1..N, 0 none)
  void status(float* out, int cap) const;

 private:
  struct Take {
    bool has = false;
    int64_t start = 0, n = 0, lat = 0, pStart = 0, pEnd = 0;
  };
  float mixAt(int64_t q, int skipTrack, bool anySolo) const;
  void processMusic(const float* in, float* out, int n, int st);
  float clickAt(double offsetFrames, bool accent) const;
  double beatFrames() const { return double(kSr) * 60.0 / double(bpm_.load()); }
  std::string place(int t, const int16_t* raw, const int16_t* ref, int64_t n, int64_t start,
                    int64_t lat, bool wipePrevious);
  int64_t maxLen() const;
  bool saveTakeFiles(int t, const int16_t* raw, const int16_t* ref, int64_t n);

  int16_t* data_[kTracks] = {};
  std::atomic<int64_t> len_[kTracks];
  std::atomic<float> gain_[kTracks];
  std::atomic<bool> mute_[kTracks];
  std::atomic<bool> solo_[kTracks];
  std::atomic<float> peak_[kTracks];
  std::atomic<float> inPeak_{0.f};
  std::atomic<int> state_{kStopped};
  std::atomic<int64_t> pos_{0};
  std::atomic<int> armed_{-1};

  int16_t* raw_ = nullptr;
  int16_t* ref_ = nullptr;
  std::atomic<int64_t> recN_{0};
  int64_t recStart_ = 0;
  int recTrack_ = -1;

  Take take_[kTracks];
  int latency_ = 0;
  int trim_ = 0;
  std::atomic<int> bpm_{100}, countIn_{0}, countBeat_{0};
  std::atomic<bool> clickPlay_{false}, countOnPlay_{false};
  std::atomic<int> pending_{kPlaying};
  int64_t cn_ = 0;
  int cleanMode_ = kCleanFixed;
  EchoPath echo_;
  std::string dir_;
  std::mutex ctl_;
};

}  // namespace fz
