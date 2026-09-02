// Geekatplay TerraForge - exact Euclidean distance transform.
// See gpx/distance.hpp for what it computes and why it is shared.
#include "gpx/distance.hpp"
#include "gpx/parallel.hpp"
#include <algorithm>
#include <cmath>

namespace gpx {
namespace {

// d[i] = min over j of (i-j)^2 + f[j]. v/z are scratch (size n and n+1).
void edt_line(const float *f, float *d, int *v, float *z, int n) {
  int k = 0;
  v[0] = 0;
  z[0] = -1e18f;
  z[1] = 1e18f;
  for (int q = 1; q < n; ++q) {
    float s = ((f[q] + (float)q * q) - (f[v[k]] + (float)v[k] * v[k])) /
              (2.f * q - 2.f * v[k]);
    while (s <= z[k]) {
      --k;
      s = ((f[q] + (float)q * q) - (f[v[k]] + (float)v[k] * v[k])) /
          (2.f * q - 2.f * v[k]);
    }
    ++k;
    v[k] = q;
    z[k] = s;
    z[k + 1] = 1e18f;
  }
  k = 0;
  for (int q = 0; q < n; ++q) {
    while (z[k + 1] < (float)q) ++k;
    d[q] = (float)(q - v[k]) * (q - v[k]) + f[v[k]];
  }
}

// squared EDT of a binary grid: 0 where inside(i), a large sentinel elsewhere.
// Row passes are independent, then column passes are independent, so the
// parallelism cannot change the result (AGENTS.md engine rule 1).
} // namespace

void edt_squared(std::vector<float> &g, int w, int h) {
  const float FAR = 1e12f; // far greater than any real squared distance
  for (float &v : g) v = v > 0.5f ? 0.f : FAR;
  parallel_rows(h, [&](int y0, int y1) {
    std::vector<float> f(w), d(w), z(w + 1);
    std::vector<int> vv(w);
    for (int y = y0; y < y1; ++y) {
      float *row = g.data() + (size_t)y * w;
      std::copy(row, row + w, f.begin());
      edt_line(f.data(), d.data(), vv.data(), z.data(), w);
      std::copy(d.begin(), d.end(), row);
    }
  });
  parallel_rows(w, [&](int x0, int x1) {
    std::vector<float> f(h), d(h), z(h + 1);
    std::vector<int> vv(h);
    for (int x = x0; x < x1; ++x) {
      for (int y = 0; y < h; ++y) f[y] = g[(size_t)y * w + x];
      edt_line(f.data(), d.data(), vv.data(), z.data(), h);
      for (int y = 0; y < h; ++y) g[(size_t)y * w + x] = d[y];
    }
  });
}


} // namespace gpx
