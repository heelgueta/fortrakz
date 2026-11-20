#include "fft.h"

#include <cmath>
#include <utility>

namespace fz {

size_t nextPow2(size_t n) {
  size_t p = 1;
  while (p < n) p <<= 1;
  return p;
}

FftPlan::FftPlan(size_t n) : n_(n), tw_(n / 2 > 0 ? n / 2 : 1), rev_(n) {
  for (size_t k = 0; k < n / 2; k++) {
    double a = -2.0 * M_PI * double(k) / double(n);
    tw_[k] = cd(std::cos(a), std::sin(a));
  }
  size_t bits = 0;
  while ((size_t(1) << bits) < n) bits++;
  for (size_t i = 0; i < n; i++) {
    uint32_t r = 0;
    for (size_t b = 0; b < bits; b++)
      if (i & (size_t(1) << b)) r |= 1u << (bits - 1 - b);
    rev_[i] = r;
  }
}

void FftPlan::run(cd* a, bool inverse) const {
  const size_t n = n_;
  for (size_t i = 0; i < n; i++)
    if (i < rev_[i]) std::swap(a[i], a[rev_[i]]);
  for (size_t len = 2; len <= n; len <<= 1) {
    const size_t half = len / 2, step = n / len;
    for (size_t i = 0; i < n; i += len) {
      for (size_t k = 0; k < half; k++) {
        cd w = tw_[k * step];
        if (inverse) w = std::conj(w);
        cd u = a[i + k], v = a[i + k + half] * w;
        a[i + k] = u + v;
        a[i + k + half] = u - v;
      }
    }
  }
  if (inverse) {
    const double s = 1.0 / double(n);
    for (size_t i = 0; i < n; i++) a[i] *= s;
  }
}

}  // namespace fz
