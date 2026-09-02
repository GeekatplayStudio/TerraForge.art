// Geekatplay TerraForge — carving along a drawn path.
//
// The path family is the reference catalogue's second-largest gap, and this
// is its working core: a polyline in tile coordinates, smoothed into a curve,
// stamped onto the grid, turned into an exact distance field, and used to
// carve — a riverbed, a road cut, a canyon, or (with negative depth) a wall
// or levee along the line.
//
// The points live in a text attribute, "x,z" pairs in 0..1 tile coordinates
// separated by whitespace or semicolons. That is deliberate v1 plumbing: it
// serialises, undoes, diffs and drives from the AI and the scripting API with
// machinery that already exists, and a viewport point editor can land later
// without changing the format. The format is documented on the node where
// the user meets it.
#include "gpx/distance.hpp"
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"
#include "gpx/parallel.hpp"
#include "gpx/planet_math.hpp"
#include <queue>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace gpx {

namespace {

struct P2 {
  float x, z;
};

// "x,z x,z; x,z" -> points; anything unparseable is skipped rather than
// guessed at
std::vector<P2> parse_points(const std::string &s) {
  std::vector<P2> pts;
  const char *c = s.c_str();
  while (*c) {
    while (*c == ' ' || *c == ';' || *c == '\n' || *c == '\t' || *c == '\r')
      ++c;
    if (!*c) break;
    char *end = nullptr;
    float x = std::strtof(c, &end);
    if (end == c) { ++c; continue; }
    c = end;
    while (*c == ' ' || *c == ',') ++c;
    float z = std::strtof(c, &end);
    if (end == c) continue;
    c = end;
    pts.push_back({x, z});
  }
  return pts;
}

// Chaikin corner cutting: each pass replaces every corner with two points a
// quarter of the way along its edges. A few passes turn a polyline into a
// smooth curve that still follows the drawn points.
std::vector<P2> chaikin(std::vector<P2> pts, int passes, bool closed) {
  for (int p = 0; p < passes && pts.size() >= 3; ++p) {
    std::vector<P2> out;
    out.reserve(pts.size() * 2);
    if (!closed) out.push_back(pts.front());
    const size_t n = pts.size();
    const size_t last = closed ? n : n - 1;
    for (size_t i = 0; i < last; ++i) {
      const P2 &a = pts[i], &b = pts[(i + 1) % n];
      out.push_back({a.x * 0.75f + b.x * 0.25f, a.z * 0.75f + b.z * 0.25f});
      out.push_back({a.x * 0.25f + b.x * 0.75f, a.z * 0.25f + b.z * 0.75f});
    }
    if (!closed) out.push_back(pts.back());
    pts = std::move(out);
  }
  return pts;
}

// Stamp the polyline onto the grid (cells the path passes through become the
// shape) and record, for each stamped cell, the terrain height at that point
// of the path — that is what lets the carve grade along the line.
void stamp_path(const std::vector<P2> &pts, bool closed, const Heightmap &ter,
                std::vector<float> &mask, std::vector<float> &path_h, int w,
                int h) {
  mask.assign((size_t)w * h, 0.f);
  path_h.assign((size_t)w * h, 0.f);
  if (pts.size() < 2) return;
  const size_t segs = closed ? pts.size() : pts.size() - 1;
  for (size_t i = 0; i < segs; ++i) {
    const P2 &a = pts[i], &b = pts[(i + 1) % pts.size()];
    const float ax = a.x * (w - 1), az = a.z * (h - 1);
    const float bx = b.x * (w - 1), bz = b.z * (h - 1);
    const int steps =
        1 + (int)std::ceil(std::max(std::fabs(bx - ax), std::fabs(bz - az)));
    for (int s = 0; s <= steps; ++s) {
      const float t = (float)s / (float)steps;
      const int x = (int)std::lround(ax + (bx - ax) * t);
      const int z = (int)std::lround(az + (bz - az) * t);
      if (x < 0 || x >= w || z < 0 || z >= h) continue;
      const size_t idx = (size_t)z * w + x;
      mask[idx] = 1.f;
      path_h[idx] = ter.v[idx];
    }
  }
}

} // namespace

REGISTER_NODE(
    PathCarve, "Path",
    "Carves along a drawn path - riverbeds, road cuts, canyons; negative depth builds walls",
    [](Node &n) {
      n.add_in("input");
      n.add_in("path", DataType::Points, true);
      n.add_out("output");
      n.add_out("path_mask");
      add_text(n.attrs, "points", "Points",
               "0.2,0.8  0.4,0.5  0.6,0.45  0.85,0.2")
          .tooltip = "The path, as x,z pairs in tile coordinates (0..1),\n"
                     "separated by spaces. Edit here, or ask the AI to\n"
                     "\"draw a river from the northwest to the sea\".";
      add_int(n.attrs, "smooth", "Smoothing", 3, 0, 6, "Shape")
          .tooltip = "Chaikin corner-cutting passes: 0 keeps the polyline's\n"
                     "corners, a few make a flowing curve.";
      add_bool(n.attrs, "closed", "Closed loop", false, "Shape");
      add_float(n.attrs, "width", "Width", 0.02f, 0.001f, 0.3f, "Profile")
          .tooltip = "Half the carve reaches this far from the line, as a\n"
                     "fraction of the tile.";
      add_float(n.attrs, "depth", "Depth", 0.08f, -0.5f, 0.5f, "Profile")
          .tooltip = "How deep the centre cuts below the surface. Negative\n"
                     "raises instead: walls, levees, causeways.";
      add_choice(n.attrs, "profile", "Profile",
                 {"Rounded (U)", "Sharp (V)", "Flat bed"}, 0, "Profile")
          .tooltip = "The cross-section: U for rivers, V for gorges, a flat\n"
                     "bed with shoulders for roads and canals.";
      add_float(n.attrs, "level", "Grade along the path", 0.5f, 0.f, 1.f,
                "Profile")
          .tooltip = "0 follows the terrain exactly - the cut is everywhere\n"
                     "the same depth. 1 grades the bed toward the path's\n"
                     "smoothed height, the way water and roadbuilders do,\n"
                     "cutting deeper through rises and shallower in dips.";
      setup_post(n);
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      Heightmap &out = n.out_hmap("output");
      Heightmap &pmask = n.out_hmap("path_mask");
      out = *in;
      pmask = *in;
      std::fill(pmask.v.begin(), pmask.v.end(), 0.f);

      std::vector<P2> pts;
      if (const PointCloud *pc = n.in_points("path")) {
        // a connected path (ordered points) overrides the typed-in list
        for (size_t i = 0; i < pc->size(); ++i)
          pts.push_back({pc->x[i], pc->y[i]});
      } else {
        const Attribute *pa = n.attrs.find("points");
        pts = parse_points(pa ? pa->s : std::string());
      }
      if (pts.size() < 2) {
        n.error = "the path needs at least two points";
        apply_post(n, out);
        return;
      }
      const bool closed = n.attrs.get_b("closed", false);
      pts = chaikin(std::move(pts), n.attrs.get_i("smooth", 3), closed);

      const int w = in->w, h = in->h;
      std::vector<float> mask, path_h;
      stamp_path(pts, closed, *in, mask, path_h, w, h);

      // distance to the line, exactly; and the path's height carried outward
      // to every cell from its nearest stamped cell would need a second
      // transform, so instead the graded bed height is smoothed along the
      // line itself and read where needed via the mask cells' influence:
      // each cell inside the width takes the height of the nearest path cell,
      // which the squared-EDT gives us implicitly by re-running it on the
      // stamped heights. Cheaper and exact enough: smooth the stamped heights
      // with a box pass over the mask, then let distance shape the profile.
      std::vector<float> dist(mask);
      edt_squared(dist, w, h);

      // grade: average the path height over a window along the line
      // (implemented as three passes of neighbour averaging over stamped
      // cells only - the line is one cell wide, so this runs along it)
      std::vector<float> graded(path_h);
      for (int pass = 0; pass < 24; ++pass) {
        std::vector<float> next(graded);
        parallel_rows(h, [&](int y0, int y1) {
          for (int y = y0; y < y1; ++y)
            for (int x = 0; x < w; ++x) {
              const size_t i = (size_t)y * w + x;
              if (mask[i] <= 0.5f) continue;
              float sum = graded[i];
              int cnt = 1;
              for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                  const int nx = x + dx, ny = y + dy;
                  if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue;
                  const size_t ni = (size_t)ny * w + nx;
                  if (mask[ni] <= 0.5f || ni == i) continue;
                  sum += graded[ni];
                  ++cnt;
                }
              next[i] = sum / (float)cnt;
            }
        });
        graded.swap(next);
      }

      // the graded height nearest each cell: a second EDT pass would give the
      // exact owner; the profile only needs it within `width` of the line,
      // where the nearest stamped cell is within a few cells of the closest
      // point - sample it by walking toward the line's stamped cells
      const float width_px = std::max(n.attrs.get_f("width", 0.02f), 1e-4f) *
                             (float)w;
      const float depth = n.attrs.get_f("depth", 0.08f);
      const int profile = n.attrs.get_choice("profile");
      const float level = n.attrs.get_f("level", 0.5f);

      parallel_rows(h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < w; ++x) {
            const size_t i = (size_t)y * w + x;
            const float d = std::sqrt(dist[i]);
            if (d >= width_px) continue;
            float t = d / width_px; // 0 at the line, 1 at the edge
            float shape;
            switch (profile) {
              case 1: shape = 1.f - t; break;                      // V
              case 2: shape = t < 0.7f ? 1.f
                                       : 1.f - (t - 0.7f) / 0.3f;  // flat bed
                break;
              default: {
                float s = 1.f - t;                                  // U
                shape = s * s * (3.f - 2.f * s);
                break;
              }
            }
            // nearest stamped cell's graded height: search the small
            // neighbourhood the distance promises it is in
            float target = in->v[i];
            if (level > 0.f) {
              const int r = (int)std::ceil(d) + 1;
              float best = 1e9f;
              for (int dy = -r; dy <= r; ++dy) {
                const int ny = y + dy;
                if (ny < 0 || ny >= h) continue;
                for (int dx = -r; dx <= r; ++dx) {
                  const int nx = x + dx;
                  if (nx < 0 || nx >= w) continue;
                  const size_t ni = (size_t)ny * w + nx;
                  if (mask[ni] <= 0.5f) continue;
                  const float dd = (float)(dx * dx + dy * dy);
                  if (dd < best) {
                    best = dd;
                    target = graded[ni];
                  }
                }
              }
            }
            const float base =
                in->v[i] + (target - in->v[i]) * level; // graded reference
            const float carved = base - depth * shape;
            // a cut only ever lowers, a wall only ever raises - so crossing
            // an existing gorge does not helpfully fill it back in
            out.v[i] = depth > 0.f ? std::min(out.v[i], carved)
                                   : std::max(out.v[i], carved);
            pmask.v[i] = std::max(pmask.v[i], shape);
          }
      });
      apply_post(n, out);
    })

REGISTER_NODE(
    PathFind, "Path", "Route a path across the terrain",
    [](Node &n) {
      n.add_in("input");
      n.add_in("cost", DataType::Heightmap, true);
      n.add_out("path", DataType::Points);
      n.add_out("path_mask");
      add_vec2(n.attrs, "start", "Start", 0.05f, 0.5f, 0.f, 1.f, "Route");
      add_vec2(n.attrs, "end", "End", 0.95f, 0.5f, 0.f, 1.f, "Route");
      add_float(n.attrs, "slope_penalty", "Slope penalty", 40.f, 0.f, 400.f,
                "Route")
          .tooltip = "How much climbing costs against walking flat. High\n"
                     "values contour around hills the way real roads do.";
      add_int(n.attrs, "simplify", "Keep every Nth point", 4, 1, 32, "Route");
    },
    [](Node &n) {
      const Heightmap *in = require_in(n, "input");
      if (!in) return;
      PointCloud &out = n.out_points("path");
      Heightmap &pmask = n.out_hmap("path_mask");
      pmask = *in;
      std::fill(pmask.v.begin(), pmask.v.end(), 0.f);
      const Heightmap *cost_map = n.in_hmap("cost");
      int w = in->w, h = in->h;
      float sx, sy, ex, ey;
      n.attrs.get_vec2("start", sx, sy);
      n.attrs.get_vec2("end", ex, ey);
      size_t start = (size_t)std::clamp((int)(sy * h), 0, h - 1) * w +
                     std::clamp((int)(sx * w), 0, w - 1);
      size_t goal = (size_t)std::clamp((int)(ey * h), 0, h - 1) * w +
                    std::clamp((int)(ex * w), 0, w - 1);
      float k = n.attrs.get_f("slope_penalty", 40.f);

      // Dijkstra over the 8-connected grid; cost of a step is its length
      // plus the slope penalty (and any extra painted cost). The heap is
      // ordered by (distance, index), a total order, so the route cannot
      // depend on evaluation order.
      std::vector<float> dist_v((size_t)w * h, 1e30f);
      std::vector<int> prev((size_t)w * h, -1);
      using QE = std::pair<float, size_t>;
      std::priority_queue<QE, std::vector<QE>, std::greater<QE>> heap;
      dist_v[start] = 0;
      heap.push({0, start});
      const int nb[8][2] = {{1, 0},  {-1, 0}, {0, 1},  {0, -1},
                            {1, 1},  {1, -1}, {-1, 1}, {-1, -1}};
      while (!heap.empty()) {
        auto [d, c] = heap.top();
        heap.pop();
        if (d > dist_v[c]) continue;
        if (c == goal) break;
        int cx = (int)(c % w), cy = (int)(c / w);
        for (auto &dxy : nb) {
          int nx = cx + dxy[0], ny = cy + dxy[1];
          if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
          size_t q = (size_t)ny * w + nx;
          float step = (dxy[0] && dxy[1]) ? 1.41421356f : 1.f;
          float dz = std::fabs(in->v[q] - in->v[c]) * w; // slope in cells
          float extra = cost_map ? std::max(cost_map->v[q], 0.f) * 10.f : 0.f;
          float nd = d + step * (1.f + k * dz / step + extra);
          if (nd < dist_v[q]) {
            dist_v[q] = nd;
            prev[q] = (int)c;
            heap.push({nd, q});
          }
        }
      }
      if (prev[goal] < 0 && goal != start) {
        n.error = "no route found";
        return;
      }
      std::vector<size_t> chain;
      for (size_t c = goal;; c = (size_t)prev[c]) {
        chain.push_back(c);
        if (c == start) break;
      }
      int keep = std::max(1, n.attrs.get_i("simplify", 4));
      for (size_t i = chain.size(); i-- > 0;) {
        size_t c = chain[i];
        pmask.v[c] = 1.f;
        bool endpoint = (i == 0) || (i == chain.size() - 1);
        if (endpoint || ((chain.size() - 1 - i) % (size_t)keep) == 0)
          out.add((c % w + 0.5f) / w, (c / w + 0.5f) / h, 0.f);
      }
    })

REGISTER_NODE(
    PathSDF, "Path", "Distance to a path",
    [](Node &n) {
      n.add_in("path", DataType::Points);
      n.add_out("distance");
      n.add_out("mask");
      add_float(n.attrs, "reach", "Reach", 0.1f, 0.005f, 1.f, "Distance");
      add_bool(n.attrs, "invert", "Invert", false, "Distance");
      add_bool(n.attrs, "closed", "Closed loop", false, "Distance");
      add_int(n.attrs, "smooth", "Smoothing", 0, 0, 6, "Distance");
    },
    [](Node &n) {
      const PointCloud *in = n.in_points("path");
      Heightmap &dist = n.out_hmap("distance");
      Heightmap &m = n.out_hmap("mask");
      if (!in || in->size() < 2) return;
      std::vector<P2> pts;
      for (size_t i = 0; i < in->size(); ++i)
        pts.push_back({in->x[i], in->y[i]});
      bool closed = n.attrs.get_b("closed", false);
      pts = chaikin(std::move(pts), n.attrs.get_i("smooth", 0), closed);
      int w = dist.w, h = dist.h;
      Heightmap flat(w, h);
      std::vector<float> stamped, unused;
      stamp_path(pts, closed, flat, stamped, unused, w, h);
      edt_squared(stamped, w, h);
      float reach = std::max(n.attrs.get_f("reach", 0.1f), 1e-4f) * w;
      bool invert = n.attrs.get_b("invert");
      parallel_index(dist.v.size(), [&](size_t i0, size_t i1) {
        for (size_t i = i0; i < i1; ++i) {
          float t = std::min(std::sqrt(stamped[i]) / reach, 1.f);
          dist.v[i] = invert ? 1.f - t : t;
          float s = 1.f - t;
          m.v[i] = s * s * (3.f - 2.f * s);
        }
      });
    })

} // namespace gpx
