#pragma once
#include <complex>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace fz {

using cd = std::complex<double>;

size_t nextPow2(size_t n);

// Radix-2 FFT plan (size must be a power of two). Not thread-safe to share
// scratch, but run() only reads the plan so one plan can be used concurrently.
class FftPlan {
 public:
  explicit FftPlan(size_t n);
  void run(cd* a, bool inverse) const;
  size_t size() const { return n_; }

 private:
  size_t n_;
  std::vector<cd> tw_;
  std::vector<uint32_t> rev_;
};

}  // namespace fz
