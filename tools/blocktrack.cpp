// Per-block delay tracking + zero-run detection on an echo capture.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include "../native/core/dsp.h"
#include "../native/core/lab.h"
using namespace fz;
static std::vector<float> readWav(const char* p) {
  FILE* f = fopen(p, "rb"); std::vector<float> v; if (!f) return v;
  char id[4]; uint32_t sz; fseek(f, 12, SEEK_SET);
  while (fread(id, 1, 4, f) == 4 && fread(&sz, 4, 1, f) == 1) {
    if (!memcmp(id, "data", 4)) { v.resize(sz / 4); fread(v.data(), 4, v.size(), f); break; }
    fseek(f, sz, SEEK_CUR);
  }
  fclose(f); return v;
}
int main(int argc, char** argv) {
  int sr = 48000; auto stim = readWav(argv[1]); auto cap = readWav(argv[2]);
  EchoStim es = makeEchoStim(sr);
  const float* ref = stim.data() + es.noiseStart; const float* mic = cap.data() + es.noiseStart;
  size_t nl = size_t(es.noiseLen);
  int D = findDelay(mic, nl + 30000, ref, nl, 30000).lag;
  // zero runs in the capture (zero-filled frames) inside the noise region
  size_t zr = 0, run = 0, zrFrames = 0;
  for (size_t i = D + sr; i < nl + D - sr; i++) { if (mic[i] == 0.f) run++; else { if (run >= 4) { zr++; zrFrames += run; } run = 0; } }
  printf("global delay %d; zero-runs(>=4) %zu covering %zu frames\n", D, zr, zrFrames);
  const size_t blk = sr / 4;
  printf("block  t(s)  lag-D  corr   (lag searched +-400 around D)\n");
  for (size_t a = size_t(sr); a + blk + 2 * sr < nl; a += blk) {
    DelayResult r = findDelay(mic + a + D - 400, blk + 800, ref + a, blk, 800);
    printf("%5zu %5.2f  %+5d  %.3f\n", a / blk, double(a) / sr, r.lag - 400, r.coherence);
  }
}
