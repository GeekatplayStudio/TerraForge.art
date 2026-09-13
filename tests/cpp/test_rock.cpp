// Geekatplay TerraForge - rocks (engine/gpx/rock.hpp).
//
// The promises:
//   1. every type builds a closed, sane mesh at every detail level;
//   2. the seed is the whole of the variation - same seed, same stone, and
//      different seeds are actually different stones rather than the same one
//      jittered;
//   3. the types differ in the way they claim to: a river stone is rounder
//      than fresh scree, a slab is flatter than a boulder, a column is
//      taller, a vesicular rock has more surface than a smooth one;
//   4. a rock sits ON the ground - its lowest point is y = 0 - and is the
//      size it was asked for;
//   5. the words people type reach the type they mean.
//
// It also writes a contact sheet of every type, shaded, to
// build/rock_sheet.png. A generator nobody looks at is a generator that
// quietly makes potatoes.
#include "gpx/rock.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <cstdio>
#include <string>
#include <vector>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

using namespace gpx;

static int failures = 0;
static void check(bool ok, const char *what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

// How sharply the surface turns, face to face: the mean angle between the
// two triangles sharing an edge, in degrees.
//
// Deviation from a sphere will not do. A river stone is a smooth ELLIPSOID,
// and measured against a sphere it scores as rough while being the smoothest
// thing in the set - the measure would be of shape, not of finish. Turning
// per edge is what "rough" means and it does not care what shape the stone
// is overall.
static float crease_degrees(const RockMesh &m) {
  // the mesh has split vertices, so adjacency has to be found by position
  std::map<std::array<int, 3>, int> weld;
  std::vector<int> id(m.vert_count(), -1);
  for (int i = 0; i < m.vert_count(); ++i) {
    const std::array<int, 3> k{(int)std::lround(m.pos[i * 3] * 8192.f),
                               (int)std::lround(m.pos[i * 3 + 1] * 8192.f),
                               (int)std::lround(m.pos[i * 3 + 2] * 8192.f)};
    auto it = weld.find(k);
    if (it == weld.end()) it = weld.emplace(k, (int)weld.size()).first;
    id[i] = it->second;
  }
  const size_t faces = m.idx.size() / 3;
  std::vector<std::array<float, 3>> fn(faces);
  for (size_t f = 0; f < faces; ++f) {
    const float *p0 = &m.pos[m.idx[f * 3] * 3], *p1 = &m.pos[m.idx[f * 3 + 1] * 3],
                *p2 = &m.pos[m.idx[f * 3 + 2] * 3];
    const float u[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
    const float v[3] = {p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]};
    float c[3] = {u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
    const float l = std::sqrt(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]);
    for (int k = 0; k < 3; ++k) fn[f][k] = l > 1e-20f ? c[k] / l : 0.f;
  }
  std::map<std::pair<int, int>, int> first_face;
  double sum = 0;
  int n = 0;
  for (size_t f = 0; f < faces; ++f)
    for (int k = 0; k < 3; ++k) {
      int a = id[m.idx[f * 3 + k]], b = id[m.idx[f * 3 + (k + 1) % 3]];
      if (a > b) std::swap(a, b);
      auto it = first_face.find({a, b});
      if (it == first_face.end()) { first_face.emplace(std::make_pair(a, b), (int)f); continue; }
      const auto &n0 = fn[(size_t)it->second], &n1 = fn[f];
      const float d = std::clamp(n0[0] * n1[0] + n0[1] * n1[1] + n0[2] * n1[2], -1.f, 1.f);
      sum += std::acos(d) * 57.29577951f;
      ++n;
    }
  return n ? (float)(sum / n) : 0.f;
}

// Surface area over the area of a sphere of the same volume-ish radius: a
// pitted rock has much more surface than a smooth one of the same size.
static float area(const RockMesh &m) {
  double a = 0;
  for (size_t f = 0; f + 2 < m.idx.size(); f += 3) {
    const float *p0 = &m.pos[m.idx[f] * 3], *p1 = &m.pos[m.idx[f + 1] * 3], *p2 = &m.pos[m.idx[f + 2] * 3];
    const float u[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
    const float v[3] = {p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]};
    const float c[3] = {u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
    a += 0.5 * std::sqrt((double)c[0] * c[0] + (double)c[1] * c[1] + (double)c[2] * c[2]);
  }
  return (float)a;
}

// ------------------------------------------------------------ contact sheet
// A plain z-buffered render, one cell per type, lit from over the left
// shoulder. No library: the point is to see the stone, not to be pretty.
struct Sheet {
  int cols, rows, cell, w, h;
  std::vector<unsigned char> px;
  Sheet(int c, int r, int s) : cols(c), rows(r), cell(s), w(c * s), h(r * s),
                               px((size_t)c * s * r * s * 3, 24) {}
  void draw(const RockMesh &m, int col, int row) {
    const int x0 = col * cell, y0 = row * cell;
    std::vector<float> z((size_t)cell * cell, -1e30f);
    float span = 0.f;
    for (int k = 0; k < 3; ++k) span = std::max(span, m.bmax[k] - m.bmin[k]);
    if (span <= 0.f) return;
    const float sc = cell * 0.36f / span;
    const float cy = (m.bmin[1] + m.bmax[1]) * 0.5f;
    const float L[3] = {-0.5f, 0.72f, 0.48f}; // over the left shoulder
    for (size_t f = 0; f + 2 < m.idx.size(); f += 3) {
      float sx[3], sy[3], sz[3], nl[3];
      bool ok = true;
      for (int k = 0; k < 3; ++k) {
        const uint32_t vi = m.idx[f + k];
        // a three-quarter view: turned 35 degrees and tipped 20
        const float X = m.pos[vi * 3], Y = m.pos[vi * 3 + 1] - cy, Z = m.pos[vi * 3 + 2];
        const float rx = X * 0.819f + Z * 0.574f;
        const float rz = -X * 0.574f + Z * 0.819f;
        const float ry = Y * 0.940f - rz * 0.342f;
        const float depth = Y * 0.342f + rz * 0.940f;
        sx[k] = cell * 0.5f + rx * sc;
        sy[k] = cell * 0.5f - ry * sc;
        sz[k] = depth;
        const float *nn = &m.nrm[vi * 3];
        nl[k] = nn[0] * L[0] + nn[1] * L[1] + nn[2] * L[2];
        if (!std::isfinite(sx[k]) || !std::isfinite(sy[k])) ok = false;
      }
      if (!ok) continue;
      const int lo_x = std::max(0, (int)std::floor(std::min({sx[0], sx[1], sx[2]})));
      const int hi_x = std::min(cell - 1, (int)std::ceil(std::max({sx[0], sx[1], sx[2]})));
      const int lo_y = std::max(0, (int)std::floor(std::min({sy[0], sy[1], sy[2]})));
      const int hi_y = std::min(cell - 1, (int)std::ceil(std::max({sy[0], sy[1], sy[2]})));
      const float d = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
      if (std::fabs(d) < 1e-9f) continue;
      for (int y = lo_y; y <= hi_y; ++y)
        for (int x = lo_x; x <= hi_x; ++x) {
          const float px_ = x + 0.5f, py_ = y + 0.5f;
          float a = ((sx[1] - px_) * (sy[2] - py_) - (sx[2] - px_) * (sy[1] - py_)) / d;
          float b = ((sx[2] - px_) * (sy[0] - py_) - (sx[0] - px_) * (sy[2] - py_)) / d;
          float c = 1.f - a - b;
          if (a < 0 || b < 0 || c < 0) continue;
          const float zz = a * sz[0] + b * sz[1] + c * sz[2];
          float &zb = z[(size_t)y * cell + x];
          if (zz <= zb) continue;
          zb = zz;
          const float lam = std::clamp(a * nl[0] + b * nl[1] + c * nl[2], 0.f, 1.f);
          const float v = 0.16f + 0.78f * lam;
          const size_t o = ((size_t)(y0 + y) * w + (x0 + x)) * 3;
          px[o] = (unsigned char)(std::clamp(v * 1.02f, 0.f, 1.f) * 255);
          px[o + 1] = (unsigned char)(std::clamp(v * 0.98f, 0.f, 1.f) * 255);
          px[o + 2] = (unsigned char)(std::clamp(v * 0.93f, 0.f, 1.f) * 255);
        }
    }
  }
};

int main() {
  // ---- 1. every type, every level ------------------------------------------
  for (int t = 0; t < (int)RockType::Count; ++t)
    for (int lv = 1; lv <= 4; ++lv) {
      RockParams p;
      p.type = (RockType)t;
      p.seed = 1234u + (uint32_t)t;
      p.detail = lv;
      RockMesh m;
      rock_build(p, m);
      const std::string who = std::string(rock_type_name(p.type)) + " level " + std::to_string(lv);
      check(m.tri_count() > 0 && m.vert_count() == m.tri_count() * 3,
            (who + ": has triangles").c_str());
      bool finite = true;
      for (float f : m.pos) finite = finite && std::isfinite(f);
      for (float f : m.nrm) finite = finite && std::isfinite(f);
      check(finite, (who + ": every number is a number").c_str());
      bool unit_normals = true;
      for (size_t i = 0; i + 2 < m.nrm.size(); i += 3) {
        const float l = std::sqrt(m.nrm[i] * m.nrm[i] + m.nrm[i+1] * m.nrm[i+1] + m.nrm[i+2] * m.nrm[i+2]);
        unit_normals = unit_normals && std::fabs(l - 1.f) < 1e-3f;
      }
      check(unit_normals, (who + ": normals are unit length").c_str());
      check(std::fabs(m.bmin[1]) < 1e-5f, (who + ": sits on the ground").c_str());
    }

  // ---- 2. the seed is the variation ----------------------------------------
  {
    RockParams a;
    a.type = RockType::Angular;
    a.seed = 77;
    RockMesh m1, m2, m3;
    rock_build(a, m1);
    rock_build(a, m2);
    check(m1.pos == m2.pos, "the same seed is the same stone");
    a.seed = 78;
    rock_build(a, m3);
    check(m1.pos.size() == m3.pos.size(), "and the same size of stone");
    double diff = 0;
    for (size_t i = 0; i < m1.pos.size(); ++i) diff += std::fabs(m1.pos[i] - m3.pos[i]);
    check(diff / std::max<size_t>(m1.pos.size(), 1) > 0.01,
          "a different seed is a different stone, not a jitter");
  }

  // ---- 3. the types differ the way they claim ------------------------------
  {
    // At one detail level throughout. A coarser mesh has larger angles
    // between its faces whatever its finish, so comparing a pebble built at
    // level 2 with a boulder at level 4 measures the tessellation and not the
    // stone - which is what it did, and said a pebble was rougher than fresh
    // scree.
    auto build = [](RockType t, uint32_t seed, int detail = 4) {
      RockParams p;
      p.type = t;
      p.seed = seed;
      p.detail = detail;
      RockMesh m;
      rock_build(p, m);
      return m;
    };
    // averaged over several seeds, so this is about the type and not one stone
    auto mean_spread = [&](RockType t) {
      float s = 0.f;
      for (uint32_t k = 0; k < 6; ++k) s += crease_degrees(build(t, 100u + k));
      return s / 6.f;
    };
    const float sharp = mean_spread(RockType::Sharp);
    const float angular = mean_spread(RockType::Angular);
    const float river = mean_spread(RockType::River);
    const float pebble = mean_spread(RockType::Pebble);
    std::printf("  roughness: sharp %.3f  angular %.3f  river %.3f  pebble %.3f\n",
                sharp, angular, river, pebble);
    check(river < angular, "a river stone is smoother than fresh scree");
    check(pebble < angular, "a pebble is smoother still");
    check(sharp > river, "a frost splinter is rougher than a river stone");

    auto shape = [&](RockType t, int axis) {
      RockMesh m = build(t, 909);
      return (m.bmax[axis] - m.bmin[axis]);
    };
    const float slab_y = shape(RockType::Slab, 1), slab_x = shape(RockType::Slab, 0);
    const float bould_y = shape(RockType::Boulder, 1), bould_x = shape(RockType::Boulder, 0);
    std::printf("  slab %.2f x %.2f, boulder %.2f x %.2f (width x height)\n",
                slab_x, slab_y, bould_x, bould_y);
    check(slab_y / slab_x < bould_y / bould_x * 0.6f, "a slab is much flatter than a boulder");
    const float col_y = shape(RockType::Columnar, 1), col_x = shape(RockType::Columnar, 0);
    check(col_y > col_x, "a column is taller than it is wide");

    // a vesicular rock has more surface than a smooth one of the same size
    const float a_volc = area(build(RockType::Volcanic, 555));
    const float a_peb = area(build(RockType::Pebble, 555));
    std::printf("  surface: volcanic %.3f  pebble %.3f\n", a_volc, a_peb);
    check(a_volc > a_peb, "gas bubbles make more surface than tumbling leaves");
  }

  // ---- 4. the size asked for ----------------------------------------------
  {
    RockParams p;
    p.type = RockType::Boulder;
    p.seed = 5;
    p.size_m = 3.5f;
    RockMesh m;
    rock_build(p, m);
    float span = 0.f;
    for (int k = 0; k < 3; ++k) span = std::max(span, m.bmax[k] - m.bmin[k]);
    check(std::fabs(span - 3.5f) < 1e-3f, "the longest axis is the size asked for");
  }

  // ---- 5. the words ------------------------------------------------------
  check(rock_type_from_words("river stone") == RockType::River, "river stone");
  check(rock_type_from_words("sharp jagged scree") == RockType::Sharp, "sharp scree");
  check(rock_type_from_words("basalt column") == RockType::Columnar, "basalt column");
  check(rock_type_from_words("sandstone flagstone") == RockType::Slab, "flagstone");
  check(rock_type_from_words("scoria") == RockType::Volcanic, "scoria");
  check(rock_type_from_words("glacial erratic") == RockType::Boulder, "erratic");
  check(rock_type_from_words("a rock") == RockType::Angular, "a plain rock");
  check(rock_type_from_words("banana") == RockType::Count, "and nothing for a banana");
  for (int t = 0; t < (int)RockType::Count; ++t) {
    check(rock_type_name((RockType)t)[0] != 0, "every type is named");
    check(rock_type_note((RockType)t)[0] != 0, "every type says what made it");
  }

  // ---- the contact sheet ---------------------------------------------------
  {
    const int n = (int)RockType::Count;
    const int cols = 5, rows = (n + cols - 1) / cols, cell = 190;
    Sheet sh(cols, rows * 2, cell); // two seeds a type, so variation shows
    for (int t = 0; t < n; ++t)
      for (int s = 0; s < 2; ++s) {
        RockParams p;
        p.type = (RockType)t;
        p.seed = 31u + (uint32_t)t * 17u + (uint32_t)s * 991u;
        p.detail = 4;
        RockMesh m;
        rock_build(p, m);
        sh.draw(m, t % cols, (t / cols) * 2 + s);
      }
    if (stbi_write_png("rock_sheet.png", sh.w, sh.h, 3, sh.px.data(), sh.w * 3))
      std::printf("  wrote rock_sheet.png (%d x %d)\n", sh.w, sh.h);
    else
      std::printf("  could not write rock_sheet.png\n");
  }

  if (failures == 0) std::printf("rocks: all checks passed\n");
  return failures == 0 ? 0 : 1;
}
