// Geekatplay TerraForge — depression filling. See gpx/hydrology.hpp for what
// it computes and why it is shared.
#include "gpx/hydrology.hpp"
#include <algorithm>
#include <cstdint>
#include <queue>
#include <vector>

namespace gpx {
namespace {

// 8-neighbourhood, in the order the analysis nodes use
const int DX8H[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
const int DY8H[8] = {-1, -1, -1, 0, 0, 1, 1, 1};

// ---- Priority-Flood depression filling ------------------------------------
// Barnes, Lehman and Mulla (2014), "Priority-flood: An optimal depression-
// filling and watershed-labeling algorithm for digital elevation models".
//
// Water can only leave the map at its edge, so start from the border and grow
// inwards, always from the lowest cell reached so far. A cell's filled height
// is then the lowest ridge any path from it to the border has to cross - which
// is exactly the level a basin fills to before it spills.
//
// Two uses, one computation:
//   - the filled surface, so flow routing does not dead-end in every hollow
//   - filled minus original, which is the depth of the lake standing there
//
// `epsilon` tilts each filled flat by a hair so water still crosses it toward
// the outlet. Zero gives true level lakes; flow routing wants a small positive
// value or a filled basin has nowhere to send its water.
//
// Deterministic: the queue orders by height and breaks ties by cell index,
// which is a total order, so the traversal - and with a non-zero epsilon the
// result - is bit-identical every run. Single-threaded, so it is independent
// of the core count as well (AGENTS.md engine rule 1).
struct FloodCell {
  float z;
  int i;
};
struct FloodCmp {
  bool operator()(const FloodCell &a, const FloodCell &b) const {
    if (a.z != b.z) return a.z > b.z; // min-heap on height
    return a.i > b.i;                 // total order: ties by index
  }
};

} // namespace

std::vector<float> fill_depressions(const Heightmap &in, float epsilon) {
  const int w = in.w, h = in.h;
  const size_t n = (size_t)w * h;
  std::vector<float> filled(in.v);
  if (w < 3 || h < 3) return filled;
  std::vector<uint8_t> closed(n, 0);
  std::priority_queue<FloodCell, std::vector<FloodCell>, FloodCmp> pq;

  auto seed = [&](int x, int y) {
    const int i = y * w + x;
    if (closed[i]) return;
    closed[i] = 1;
    pq.push({in.v[i], i});
  };
  for (int x = 0; x < w; ++x) {
    seed(x, 0);
    seed(x, h - 1);
  }
  for (int y = 0; y < h; ++y) {
    seed(0, y);
    seed(w - 1, y);
  }

  while (!pq.empty()) {
    const FloodCell c = pq.top();
    pq.pop();
    const int x = c.i % w, y = c.i / w;
    for (int k = 0; k < 8; ++k) {
      const int nx = x + DX8H[k], ny = y + DY8H[k];
      if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue;
      const int ni = ny * w + nx;
      if (closed[ni]) continue;
      closed[ni] = 1;
      filled[ni] = std::max(in.v[ni], c.z + epsilon);
      pq.push({filled[ni], ni});
    }
  }
  return filled;
}


} // namespace gpx
