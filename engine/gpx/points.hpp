// Geekatplay TerraForge - the point-cloud domain: a set of 2D points over
// the unit tile, each with a value. The third port payload next to rasters
// and fields; scatter nodes make them, filters thin them, stamp nodes turn
// them back into rasters. Coordinates are 0..1 like every mask.
//
// A population needs more than a position: which species stands there, how
// big, turned which way, leaning how far into the slope, what tint, and a
// stable identity so an edit moves only the instances it concerns. Those
// are the optional channels below. They are empty until a node writes them
// (`ensure_attrs`), so every older node keeps working on x, y, v alone and
// every consumer reads a missing channel as its default.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace gpx {

struct PointCloud {
  // parallel arrays: position (0..1 tile space) and a per-point value
  // (amplitude, radius scale, species id... whatever the consumer reads)
  std::vector<float> x, y, v;

  // ---- optional per-point channels; all empty or all of size() ----------
  std::vector<uint64_t> id;    // stable identity: hash(seed, cell, index)
  std::vector<uint16_t> species; // index into the population list
  std::vector<float> sx, sy, sz; // per-axis size multipliers (1 = base)
  std::vector<float> yaw;      // radians about the up axis
  std::vector<float> tilt;     // 0 = grows vertically, 1 = follows the surface
  std::vector<float> tint;     // brightness multiplier (1 = as authored)
  std::vector<float> phase;    // animation phase offset, 0..1
  std::vector<float> radius;   // footprint radius, tile units (spacing)
  std::vector<float> offset;   // lift above the surface, tile units

  size_t size() const { return x.size(); }
  bool has_attrs() const { return !id.empty() && id.size() == x.size(); }

  void add(float px, float py, float pv) {
    x.push_back(px);
    y.push_back(py);
    v.push_back(pv);
    if (!id.empty()) push_defaults(x.size() - 1);
  }
  void clear() {
    x.clear(); y.clear(); v.clear();
    id.clear(); species.clear(); sx.clear(); sy.clear(); sz.clear();
    yaw.clear(); tilt.clear(); tint.clear(); phase.clear(); radius.clear();
    offset.clear();
  }
  // Give every point the full channel set, defaults where nothing was set.
  // Ids of points that had none are derived from their index, so a cloud
  // made by an older node still has a stable identity downstream.
  void ensure_attrs() {
    const size_t n = x.size();
    if (id.size() != n) {
      size_t was = id.size();
      id.resize(n);
      for (size_t i = was; i < n; ++i) id[i] = 0x9E3779B97F4A7C15ull * (i + 1);
    }
    auto fill = [n](std::vector<float> &c, float d) { if (c.size() != n) c.resize(n, d); };
    if (species.size() != n) species.resize(n, 0);
    fill(sx, 1.f); fill(sy, 1.f); fill(sz, 1.f);
    fill(yaw, 0.f); fill(tilt, 0.f); fill(tint, 1.f); fill(phase, 0.f);
    fill(radius, 0.f); fill(offset, 0.f);
  }
  // Keep the points whose flag is set, in order. Every channel follows.
  void compact(const std::vector<uint8_t> &keep) {
    size_t w = 0;
    const size_t n = x.size();
    const bool attrs = has_attrs();
    for (size_t r = 0; r < n; ++r) {
      if (!keep[r]) continue;
      if (w != r) {
        x[w] = x[r]; y[w] = y[r]; v[w] = v[r];
        if (attrs) {
          id[w] = id[r]; species[w] = species[r];
          sx[w] = sx[r]; sy[w] = sy[r]; sz[w] = sz[r];
          yaw[w] = yaw[r]; tilt[w] = tilt[r]; tint[w] = tint[r];
          phase[w] = phase[r]; radius[w] = radius[r]; offset[w] = offset[r];
        }
      }
      ++w;
    }
    x.resize(w); y.resize(w); v.resize(w);
    if (attrs) {
      id.resize(w); species.resize(w); sx.resize(w); sy.resize(w); sz.resize(w);
      yaw.resize(w); tilt.resize(w); tint.resize(w); phase.resize(w);
      radius.resize(w); offset.resize(w);
    }
  }
  // Memory the cloud holds, for the graph's buffer ceiling.
  size_t bytes() const {
    size_t b = (x.size() + y.size() + v.size()) * sizeof(float);
    b += id.size() * sizeof(uint64_t) + species.size() * sizeof(uint16_t);
    b += (sx.size() + sy.size() + sz.size() + yaw.size() + tilt.size() +
          tint.size() + phase.size() + radius.size() + offset.size()) *
         sizeof(float);
    return b;
  }

private:
  void push_defaults(size_t i) {
    id.push_back(0x9E3779B97F4A7C15ull * (i + 1));
    species.push_back(0);
    sx.push_back(1.f); sy.push_back(1.f); sz.push_back(1.f);
    yaw.push_back(0.f); tilt.push_back(0.f); tint.push_back(1.f);
    phase.push_back(0.f); radius.push_back(0.f); offset.push_back(0.f);
  }
};

} // namespace gpx
