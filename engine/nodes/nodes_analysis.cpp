// Geekatplay TerraForge — terrain analysis (P1).
//
// Vue keeps a family of heightfield nodes that *measure* the terrain rather
// than change it (manual p978-994), and the measurements are what drive
// believable material distribution: grass where water collects, rock where it
// runs off, moss in the hollows.
//
// The two that matter most are here, and they are hydrological rather than
// merely geometric. Slope and curvature already exist as mask selectors; what
// was missing is any notion of *where the water goes*, which is a global
// property of the whole surface and cannot be had from a local neighbourhood.
//
// Determinism (AGENTS.md, engine rule 1) needs care in this file: flow
// accumulation walks cells in height order, and any tie in that order would
// otherwise be broken by whatever the sort happened to do. Ties are broken by
// index so the order is total and the result is bit-identical every run.
#include "gpx/node_graph.hpp"
#include "gpx/hydrology.hpp"
#include "gpx/node_helpers.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace gpx {
namespace {

// D8: every cell drains to whichever of its eight neighbours lies steepest
// downhill. Simple, stable, and the standard basis for both the wetness index
// and stream networks.
const int DX8[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
const int DY8[8] = {-1, -1, -1, 0, 0, 1, 1, 1};

// Upslope contributing area per cell, in cell counts. Every cell starts owning
// itself and pushes its total to its receiver, processed from high to low so a
// cell is always complete before it drains.
std::vector<float> flow_accumulate(const Heightmap &in, std::vector<int> *recv_out,
                                   bool fill_pits) {
  const int w = in.w, h = in.h;
  const size_t n = (size_t)w * h;

  // D8 on a raw heightfield dead-ends in every hollow: a pit cell has no
  // downhill neighbour, so its receiver is -1 and everything that drained into
  // it stops there. On real terrain - and on anything that has been eroded -
  // that is most of the surface, and it is why an unfilled accumulation map
  // shows streams that stop halfway down a valley for no visible reason.
  //
  // So route over the depression-filled surface instead (Priority-Flood, see
  // nodes_hydro.cpp). The heights used for routing change; the terrain does
  // not. A hair of epsilon tilts the filled flats so water crosses them toward
  // the outlet rather than pooling on a perfectly level lake with nowhere to
  // go.
  std::vector<float> routed;
  if (fill_pits) routed = fill_depressions(in, 1e-6f);
  const float *z = fill_pits ? routed.data() : in.v.data();

  std::vector<int> order(n);
  std::iota(order.begin(), order.end(), 0);
  // descending height, ties by index: a total order, so the walk is
  // reproducible rather than merely usually-the-same
  std::sort(order.begin(), order.end(), [&](int a, int b) {
    if (z[a] != z[b]) return z[a] > z[b];
    return a < b;
  });

  std::vector<int> receiver(n, -1);
  std::vector<float> area(n, 1.f);
  for (int idx : order) {
    const int x = idx % w, y = idx / w;
    const float hgt = z[idx];
    float best = 0.f;
    for (int k = 0; k < 8; ++k) {
      const int nx = x + DX8[k], ny = y + DY8[k];
      if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue;
      // normalise by distance so diagonals do not win on length alone
      const float drop =
          (hgt - z[ny * w + nx]) / ((DX8[k] && DY8[k]) ? 1.4142136f : 1.f);
      if (drop > best) {
        best = drop;
        receiver[idx] = ny * w + nx;
      }
    }
    if (receiver[idx] >= 0) area[receiver[idx]] += area[idx];
  }
  if (recv_out) *recv_out = std::move(receiver);
  return area;
}

// Local slope as a gradient magnitude, normalised by the terrain's own
// amplitude so the result does not depend on how tall the map happens to be.
float slope_at(const Heightmap &in, int x, int y, float amp) {
  float dx, dy;
  in.gradient_at(x, y, dx, dy);
  return std::sqrt(dx * dx + dy * dy) * in.w / amp;
}

} // namespace

// ------------------------------------------------------- flow accumulation
REGISTER_NODE(
    FlowAccumulation, "Analysis",
    "How much water passes through each point — the basis of streams and erosion patterns",
    [](Node &n) {
      n.add_in("input");
      n.add_out("output");
      add_bool(n.attrs, "log_scale", "Logarithmic", true)
          .tooltip = "Accumulation spans several orders of magnitude — a few\n"
                     "channels carry almost everything. Without this the map\n"
                     "is black with a handful of bright lines.";
      add_float(n.attrs, "threshold", "Channel threshold", 0.f, 0.f, 1.f)
          .tooltip = "Discards everything below this fraction, leaving only\n"
                     "the established channels.";
      add_bool(n.attrs, "fill_pits", "Route through basins", true)
          .tooltip = "Water that reaches a hollow fills it and flows on.\n"
                     "Off follows the raw surface, where every stream stops\n"
                     "at the first pit it meets.";
      setup_post(n);
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      // Sized from the input, not from the graph resolution: `area` has one
      // entry per input cell, and Resample or an imported heightmap upstream
      // makes those two counts differ.
      out = *in;
      std::vector<float> area =
          flow_accumulate(*in, nullptr, n.attrs.get_b("fill_pits", true));
      const bool logs = n.attrs.get_b("log_scale", true);
      parallel_index(out.v.size(), [&](size_t i0, size_t i1) {
        for (size_t i = i0; i < i1; ++i)
          out.v[i] = logs ? std::log(area[i] + 1.f) : area[i];
      });
      const float thr = n.attrs.get_f("threshold", 0.f);
      if (thr > 1e-6f) {
        float mn, mx;
        out.minmax(mn, mx);
        const float cut = mn + (mx - mn) * thr;
        parallel_index(out.v.size(), [&](size_t i0, size_t i1) {
          for (size_t i = i0; i < i1; ++i)
            if (out.v[i] < cut) out.v[i] = mn;
        });
      }
      apply_post(n, out);
    })

// ----------------------------------------------------------- wetness index
// The topographic wetness index, ln(a / tan b): large where a lot of water
// arrives and the ground is flat, small on steep ground that sheds it. This is
// the standard measure in real terrain analysis and it is what makes a
// vegetation or moss mask sit where a person would expect it to.
REGISTER_NODE(
    WetnessIndex, "Analysis",
    "Where water collects — high in flat hollows fed from above, low on steep ground",
    [](Node &n) {
      n.add_in("input");
      n.add_out("output");
      add_float(n.attrs, "min_slope", "Minimum slope", 0.01f, 1e-4f, 0.5f)
          .tooltip = "Perfectly flat ground would divide by zero and give an\n"
                     "infinitely wet pixel. This is the flattest slope the\n"
                     "index will consider.";
      add_float(n.attrs, "contrast", "Contrast", 1.f, 0.1f, 4.f);
      add_bool(n.attrs, "fill_pits", "Route through basins", true)
          .tooltip = "Water that reaches a hollow fills it and flows on.\n"
                     "Off follows the raw surface, where every stream stops\n"
                     "at the first pit it meets.";
      setup_post(n);
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      out = *in; // see the note in FlowAccumulation
      std::vector<float> area =
          flow_accumulate(*in, nullptr, n.attrs.get_b("fill_pits", true));

      float mn, mx;
      in->minmax(mn, mx);
      const float amp = (mx - mn) > 1e-12f ? mx - mn : 1.f;
      const float min_slope = std::max(n.attrs.get_f("min_slope", 0.01f), 1e-5f);
      const float contrast = n.attrs.get_f("contrast", 1.f);

      parallel_rows(in->h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < in->w; ++x) {
            const float s = std::max(slope_at(*in, x, y, amp), min_slope);
            const size_t i = (size_t)y * in->w + x;
            out.v[i] = std::log((area[i] + 1.f) / s) * contrast;
          }
      });
      apply_post(n, out);
    })

// ---------------------------------------------------------------- resample
// Vue's terrain resolution operations (p522). Working at a lower resolution
// and resampling up is the standard way to keep an expensive solver tractable,
// and halving first is how you get a smooth base to add detail onto.
REGISTER_NODE(
    Resample, "Analysis",
    "Rebuilds the terrain at a coarser or finer sampling — detail control, not size",
    [](Node &n) {
      n.add_in("input");
      n.add_out("output");
      add_choice(n.attrs, "mode", "Sampling",
                 {"Half", "Quarter", "Double", "Custom"}, 0)
          .tooltip = "Coarser sampling discards fine detail, which is how you\n"
                     "get a smooth base to build on. Finer sampling cannot\n"
                     "invent detail — it interpolates.";
      add_int(n.attrs, "custom", "Custom size", 256, 8, 8192);
      add_bool(n.attrs, "smooth", "Smooth interpolation", true)
          .tooltip = "Off: nearest neighbour, which keeps hard edges and gives\n"
                     "a deliberately blocky, terraced look.";
      setup_post(n);
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");

      int inter = in->w;
      switch (n.attrs.get_choice("mode")) {
        case 0: inter = std::max(2, in->w / 2); break;
        case 1: inter = std::max(2, in->w / 4); break;
        case 2: inter = in->w * 2; break;
        default: inter = std::clamp(n.attrs.get_i("custom", 256), 2, 8192); break;
      }
      const bool smooth = n.attrs.get_b("smooth", true);

      // Down to the intermediate sampling and back out to the graph's
      // resolution: the node changes the *detail*, not the buffer size, so
      // everything downstream keeps working at one resolution.
      Heightmap mid(inter, inter);
      parallel_rows(inter, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < inter; ++x) {
            const float u = inter > 1 ? x / float(inter - 1) : 0.f;
            const float v = inter > 1 ? y / float(inter - 1) : 0.f;
            mid.at(x, y) =
                smooth ? in->sample(u, v)
                       : in->at(std::min((int)(u * in->w), in->w - 1),
                                std::min((int)(v * in->h), in->h - 1));
          }
      });
      parallel_rows(out.h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < out.w; ++x) {
            const float u = out.w > 1 ? x / float(out.w - 1) : 0.f;
            const float v = out.h > 1 ? y / float(out.h - 1) : 0.f;
            out.at(x, y) =
                smooth ? mid.sample(u, v)
                       : mid.at(std::min((int)(u * inter), inter - 1),
                                std::min((int)(v * inter), inter - 1));
          }
      });
      apply_post(n, out);
    })

} // namespace gpx
