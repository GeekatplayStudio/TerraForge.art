// Geekatplay TerraForge — blue-noise dither pattern (void-and-cluster).
// The pattern is generated without touching GL, so it can be tested without a
// context. Uploading it lives with the other procedural noise textures in
// cloud_noise.cpp.
#include "blue_noise.hpp"
#include <cmath>
#include <vector>

namespace studio {

namespace {

// Ulichney's method needs, at every step, the tightest cluster of ones and the
// largest void among the zeros. Both come from the same Gaussian-filtered copy
// of the binary pattern: the highest filtered value sits in the densest
// cluster, the lowest in the emptiest void.
//
// Recomputing that filter from scratch each step would be O(N^2 k^2). Instead
// the filtered array is kept and updated incrementally: toggling one pixel
// adds or subtracts one kernel footprint. The pattern wraps, which is what
// makes the result tileable.
struct Energy {
  int n = 0, r = 0;
  std::vector<float> field; // filtered value per pixel
  std::vector<float> kern;  // (2r+1)^2 Gaussian

  Energy(int n_, float sigma) : n(n_) {
    r = (int)std::ceil(sigma * 2.f);
    if (r > n / 2) r = n / 2;
    kern.resize((size_t)(2 * r + 1) * (2 * r + 1));
    for (int dy = -r; dy <= r; ++dy)
      for (int dx = -r; dx <= r; ++dx)
        kern[(size_t)(dy + r) * (2 * r + 1) + (dx + r)] =
            std::exp(-(float)(dx * dx + dy * dy) / (2.f * sigma * sigma));
    field.assign((size_t)n * n, 0.f);
  }

  void splat(int x, int y, float sign) {
    for (int dy = -r; dy <= r; ++dy) {
      int yy = ((y + dy) % n + n) % n;
      for (int dx = -r; dx <= r; ++dx) {
        int xx = ((x + dx) % n + n) % n;
        field[(size_t)yy * n + xx] +=
            sign * kern[(size_t)(dy + r) * (2 * r + 1) + (dx + r)];
      }
    }
  }

  // Tightest cluster: the highest energy among pixels that are set.
  // Largest void: the lowest energy among pixels that are clear.
  // Ties break on index so the result does not depend on iteration order.
  int extreme(const std::vector<uint8_t> &bits, uint8_t want, bool highest) const {
    int best = -1;
    float best_e = 0.f;
    for (size_t i = 0; i < bits.size(); ++i) {
      if (bits[i] != want) continue;
      float e = field[i];
      if (best < 0 || (highest ? e > best_e : e < best_e)) {
        best = (int)i;
        best_e = e;
      }
    }
    return best;
  }
};

// A small deterministic generator, so the pattern is the same everywhere.
struct Rng {
  uint32_t s;
  uint32_t next() {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
  }
};

} // namespace

std::vector<float> blue_noise_pattern(int n, uint32_t seed) {
  if (n < 4) n = 4;
  const size_t N = (size_t)n * n;
  std::vector<float> out(N, 0.f);
  std::vector<uint8_t> bits(N, 0);
  // Ulichney's paper suggests 1.5. Measured across 1.2 / 1.5 / 1.9 / 2.4 on a
  // 64x64 pattern, 1.2 gives the strongest short-range decorrelation — mean
  // adjacent difference 0.427 against white noise's 0.333, where 1.5 gives
  // 0.394 and 1.9 gives 0.362. Adjacent pixels differing is exactly the
  // property the dither needs, so 1.2 it is.
  Energy e(n, 1.2f);

  // Start from a sparse random scatter — the exact starting set does not
  // matter, because the next loop rearranges it into a well-spread one.
  Rng rng{seed ? seed : 1u};
  size_t ones = N / 10;
  if (ones < 1) ones = 1;
  for (size_t placed = 0; placed < ones;) {
    size_t i = rng.next() % N;
    if (bits[i]) continue;
    bits[i] = 1;
    e.splat((int)(i % n), (int)(i / n), +1.f);
    ++placed;
  }

  // Phase 0: move the tightest cluster into the largest void, repeatedly,
  // until doing so would put it straight back. That is the initial binary
  // pattern, and it is as evenly spread as this many points can be.
  for (size_t guard = 0; guard < N * 4; ++guard) {
    int c = e.extreme(bits, 1, true);
    if (c < 0) break;
    bits[c] = 0;
    e.splat(c % n, c / n, -1.f);
    int v = e.extreme(bits, 0, false);
    if (v < 0 || v == c) { // it belongs where it was: settled
      bits[c] = 1;
      e.splat(c % n, c / n, +1.f);
      break;
    }
    bits[v] = 1;
    e.splat(v % n, v / n, +1.f);
  }

  const std::vector<uint8_t> ibp = bits;
  const std::vector<float> ibp_field = e.field;

  // Phase 1: unbuild the initial pattern, tightest cluster first. Those points
  // get the lowest ranks, so the darkest values in the finished pattern are
  // the ones that were most crowded.
  size_t rank = ones;
  while (true) {
    int c = e.extreme(bits, 1, true);
    if (c < 0) break;
    bits[c] = 0;
    e.splat(c % n, c / n, -1.f);
    out[(size_t)c] = (float)(--rank);
  }

  // Phases 2 and 3: from the initial pattern again, fill the largest void
  // each step, all the way to full. One loop rather than two, because the
  // procedure does not change at the halfway point — only Ulichney's
  // description of it does.
  bits = ibp;
  e.field = ibp_field;
  for (rank = ones; rank < N; ++rank) {
    int v = e.extreme(bits, 0, false);
    if (v < 0) break;
    bits[v] = 1;
    e.splat(v % n, v / n, +1.f);
    out[(size_t)v] = (float)rank;
  }

  for (float &f : out) f /= (float)N; // [0,1)
  return out;
}

} // namespace studio
