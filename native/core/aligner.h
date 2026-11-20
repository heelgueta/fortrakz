#pragma once
// Rigid input/output alignment for full-duplex audio.
//
// The old approach polled the input inside the output callback and dropped or
// zero-filled frames to keep the backlog small. Every drop/fill shifted the
// mic signal against the playback mid-take (seen in real captures as a sudden
// 128-frame jump and runs of zeros), which ruined echo cancellation.
//
// Here the input callback appends to a ring indexed by an ABSOLUTE frame
// counter, and the output callback reads the frames at (outputFrame + K) where
// K is fixed once per session. Nothing is ever dropped or inserted: if data is
// late the missing frames are zeros but the mapping does not move.
#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

namespace fz {

class InputAligner {
 public:
  explicit InputAligner(size_t capacityPow2 = 1 << 17) : ring_(capacityPow2, 0.f), mask_(capacityPow2 - 1) {}

  void reset() {
    inTotal_.store(0);
    outTotal_ = 0;
    k_ = 0;
    primed_ = false;
    late_ = 0;
    resyncs_ = 0;
  }

  // input thread
  void write(const float* x, int n) {
    int64_t t = inTotal_.load(std::memory_order_relaxed);
    for (int i = 0; i < n; i++) ring_[size_t(t + i) & mask_] = x[i];
    inTotal_.store(t + n, std::memory_order_release);
  }

  int64_t written() const { return inTotal_.load(std::memory_order_acquire); }

  // output thread: fills dst with the n input frames aligned to the next n output frames.
  // `cushion` = how far behind "now" the mapping is placed so late callbacks are rare.
  void read(float* dst, int n, int64_t cushion) {
    const int64_t have = inTotal_.load(std::memory_order_acquire);
    if (!primed_) {
      k_ = have - n - cushion;  // out index 0 <-> in index k_ (may be negative: zeros until data exists)
      primed_ = true;
    }
    int64_t need = outTotal_ + k_;
    const int64_t cap = int64_t(ring_.size());
    // fell hopelessly behind/ahead (stall, or clocks drifted > cushion): re-anchor, count it
    if (need < have - cap + n || need > have + 4 * cushion + 4 * int64_t(n)) {
      k_ = have - n - cushion - outTotal_;
      need = outTotal_ + k_;
      resyncs_++;
    }
    for (int i = 0; i < n; i++) {
      int64_t idx = need + i;
      if (idx < 0 || idx >= have) {
        dst[i] = 0.f;
        if (idx >= have) late_++;
      } else {
        dst[i] = ring_[size_t(idx) & mask_];
      }
    }
    outTotal_ += n;
  }

  int64_t lateFrames() const { return late_; }
  int resyncs() const { return resyncs_; }
  int64_t offset() const { return k_; }

 private:
  std::vector<float> ring_;
  size_t mask_;
  std::atomic<int64_t> inTotal_{0};
  int64_t outTotal_ = 0, k_ = 0, late_ = 0;
  int resyncs_ = 0;
  bool primed_ = false;
};

}  // namespace fz
