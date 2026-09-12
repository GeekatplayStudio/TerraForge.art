// Geekatplay TerraForge - the CutoutLeaf part: a leaf cut to its picture.
//
// A card wastes most of its texels on transparent corners and the renderer
// still shades them; a cut-out leaf meshes only the inside of the material's
// outline (PlantMaterial::cutout, u,v pairs in picture space, traced from the
// picture or drawn by hand) so a maple leaf costs its own area and no more.
// The outline is laid over a grid - the grid's cells that fall inside become
// quads split into triangles, and the ring of cells the outline crosses is
// clipped to the polygon and ear-clipped - then the flat sheet is bent along
// its axis, twisted, folded at its rib and drooped, all as functions of the
// primal v (0 at the hook end, 1 at the tip) and the lateral u (0 left,
// 1 right, the rib at 0.5).
//
// Where the design is silent: the picture's v runs from the leaf's base to
// its tip (v = 0 at the stalk); the length parameter is the leaf's along-axis
// size, and width_tweak stretches only across, so the outline's own aspect
// ratio survives (the cutout editor's "scale factor" is 1 here); the
// Catmull-Clark boost rounds the outline by that many rounds of mid-point
// smoothing; lateral_profile displaces across by its value in leaf widths;
// gravitropism bends the tip toward the ground by 45 degrees per unit; the
// breeze flexibility curve is accumulated along the axis (its running
// integral, normalised) so the tip always moves at least as much as the
// base; the material slot passed to material_for is the 0-based index among
// the "material", "material 2".. ports.
#include "plant/plant_internal.hpp"
#include <algorithm>

namespace gpx {
namespace plant {

namespace {

float transform_block(const BuildCtx &ctx, const ParamReader &pr, Instance &inst, V3 &axis_scale) {
  float size = ctx.scale * pr.random("scale", 0.f, 0, 1.f);
  if (pr.b("scale_inherit", true)) size *= inst.scale;
  axis_scale = V3(pr.random("scale_x", 0.f, 0, 1.f), pr.random("scale_y", 0.f, 0, 1.f), pr.random("scale_z", 0.f, 0, 1.f));
  Frame &f = inst.frame;
  f.o += f.dir(V3(pr.random("offset_x"), pr.random("offset_y"), pr.random("offset_z"))) * size;
  const float rx = pr.random("rot_x"), ry = pr.random("rot_y"), rz = pr.random("rot_z");
  if (rx != 0.f) f = f.rotated(f.x, rx * DEG);
  if (ry != 0.f) f = f.rotated(f.y, ry * DEG);
  if (rz != 0.f) f = f.rotated(f.z, rz * DEG);
  return size;
}

struct P2 { float x, y; };

// The default outline when the material has none: an ovate leaf, its stalk
// at v = 0, drawn as 24 points.
std::vector<P2> default_outline() {
  std::vector<P2> o;
  const int N = 24;
  for (int i = 0; i < N; ++i) {
    const float t = (float)i / (float)N * TAU;
    const float v = 0.5f - 0.5f * std::cos(t);            // 0 at the stalk, 1 at the tip
    const float w = std::sin(t) * (0.5f - 0.25f * v);       // widest below the middle
    o.push_back({0.5f + w, v});
  }
  return o;
}

float signed_area(const std::vector<P2> &p) {
  float a = 0.f;
  for (size_t i = 0, j = p.size() - 1; i < p.size(); j = i++) a += (p[j].x * p[i].y - p[i].x * p[j].y);
  return a * 0.5f;
}

bool inside(const std::vector<P2> &poly, float x, float y) {
  bool in = false;
  for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
    const P2 &a = poly[i], &b = poly[j];
    if ((a.y > y) != (b.y > y) && x < (b.x - a.x) * (y - a.y) / (b.y - a.y + 1e-20f) + a.x) in = !in;
  }
  return in;
}

// Sutherland-Hodgman: the polygon clipped to the cell [x0,x1]x[y0,y1].
std::vector<P2> clip_cell(const std::vector<P2> &poly, float x0, float y0, float x1, float y1) {
  std::vector<P2> in = poly, outp;
  auto pass = [&](int axis, float bound, bool keep_greater) {
    outp.clear();
    if (in.empty()) return;
    for (size_t i = 0, j = in.size() - 1; i < in.size(); j = i++) {
      const P2 &a = in[j], &b = in[i];
      const float va = axis ? a.y : a.x, vb = axis ? b.y : b.x;
      const bool ina = keep_greater ? va >= bound : va <= bound;
      const bool inb = keep_greater ? vb >= bound : vb <= bound;
      if (ina != inb) {
        const float t = (bound - va) / (vb - va + 1e-20f);
        outp.push_back({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t});
      }
      if (inb) outp.push_back(b);
    }
    in.swap(outp);
  };
  pass(0, x0, true); pass(0, x1, false); pass(1, y0, true); pass(1, y1, false);
  return in;
}

// Ear clipping of a simple polygon (counter-clockwise) into index triples.
void ear_clip(const std::vector<P2> &p, std::vector<int> &tris) {
  std::vector<int> idx(p.size());
  for (size_t i = 0; i < p.size(); ++i) idx[i] = (int)i;
  auto crossz = [&](int a, int b, int c) {
    return (p[(size_t)b].x - p[(size_t)a].x) * (p[(size_t)c].y - p[(size_t)a].y) -
           (p[(size_t)b].y - p[(size_t)a].y) * (p[(size_t)c].x - p[(size_t)a].x);
  };
  int guard = 0;
  while (idx.size() > 3 && guard++ < 4096) {
    bool cut = false;
    for (size_t i = 0; i < idx.size(); ++i) {
      const int a = idx[(i + idx.size() - 1) % idx.size()], b = idx[i], c = idx[(i + 1) % idx.size()];
      if (crossz(a, b, c) <= 1e-12f) continue;
      bool empty = true;
      for (int k : idx) {
        if (k == a || k == b || k == c) continue;
        if (crossz(a, b, k) >= 0 && crossz(b, c, k) >= 0 && crossz(c, a, k) >= 0) { empty = false; break; }
      }
      if (!empty) continue;
      tris.push_back(a); tris.push_back(b); tris.push_back(c);
      idx.erase(idx.begin() + (long)i);
      cut = true;
      break;
    }
    if (!cut) break;
  }
  if (idx.size() == 3) { tris.push_back(idx[0]); tris.push_back(idx[1]); tris.push_back(idx[2]); }
}

} // namespace

void build_cutout_leaf(BuildCtx &ctx, const Node &n, Instance &inst, MeshOut &out, Sockets &sk) {
  ParamReader pr(ctx, n, inst);
  const SeasonLook look = season_look(ctx, pr);
  V3 axis_scale;
  const float size = transform_block(ctx, pr, inst, axis_scale);

  // materials and the outline
  const int planes = pr.choice("planes") + 1;
  std::vector<int> slots;
  {
    const std::vector<const Node *> mats = ctx.materials_of(n);
    for (size_t i = 0; i < mats.size(); ++i) if (mats[i]) slots.push_back((int)i);
  }
  std::vector<int> plane_mat((size_t)planes, 0);
  if (!slots.empty()) {
    Draw d(ctx.seed, inst.id, hash_str("mat_distribution"));
    const int mode = pr.choice("mat_distribution", 1), m = (int)slots.size();
    if (mode == 2 && m % planes == 0) {
      const int g = (int)d.pick((uint32_t)(m / planes));
      for (int k = 0; k < planes; ++k) plane_mat[(size_t)k] = slots[(size_t)(g * planes + k)];
    } else if (mode == 0) {
      for (int k = 0; k < planes; ++k) plane_mat[(size_t)k] = slots[d.pick((uint32_t)m)];
    } else {
      const int one = slots[d.pick((uint32_t)m)];
      for (int k = 0; k < planes; ++k) plane_mat[(size_t)k] = one;
    }
  }
  const int material0 = ctx.material_for(n, plane_mat[0]);
  std::vector<P2> outline;
  if (ctx.mesh && material0 >= 0 && (size_t)material0 < ctx.mesh->materials.size()) {
    const std::vector<float> &c = ctx.mesh->materials[(size_t)material0].cutout;
    for (size_t i = 0; i + 1 < c.size(); i += 2) outline.push_back({c[i], c[i + 1]});
  }
  if (outline.size() < 3) outline = default_outline();
  if (signed_area(outline) < 0.f) std::reverse(outline.begin(), outline.end());
  {
    int rounds = pr.i("catmull");
    if (pr.b("lod_affects_catmull", true)) rounds -= ctx.lod;
    for (int r = 0; r < std::min(8, rounds); ++r) {
      std::vector<P2> s;
      for (size_t i = 0; i < outline.size(); ++i) {
        const P2 &a = outline[i], &b = outline[(i + 1) % outline.size()];
        s.push_back({a.x * 0.75f + b.x * 0.25f, a.y * 0.75f + b.y * 0.25f});
        s.push_back({a.x * 0.25f + b.x * 0.75f, a.y * 0.25f + b.y * 0.75f});
      }
      outline.swap(s);
    }
  }
  float umin = 1.f, umax = 0.f, vmin = 1.f, vmax = 0.f;
  for (const P2 &p : outline) { umin = std::min(umin, p.x); umax = std::max(umax, p.x); vmin = std::min(vmin, p.y); vmax = std::max(vmax, p.y); }
  const float uspan = std::max(1e-3f, umax - umin), vspan = std::max(1e-3f, vmax - vmin);

  // the sheet's size and its bends
  const float L = std::max(1e-4f, pr.random("length", 0.f, 0, 0.1f) * size * (1.f - look.shrink));
  const float W = L * (uspan / vspan) * pr.random("width_tweak", 0.f, 0, 1.f);
  inst.length = L;
  inst.radius = W * 0.5f;
  const float bend = pr.random("axis_bend") * DEG;
  const float twist = pr.random("twist") * TAU;
  const float rib = pr.random("midrib_angle") * DEG;
  const bool smooth_rib = pr.b("smooth_rib", true);
  const float gravi = pr.random("gravitropism") * 45.f * DEG;
  const Curve &axis_curve = pr.curve("axis_curve");
  const Curve &rib_curve = pr.curve("midrib_curve");
  const Curve &lateral = pr.curve("lateral_profile");

  if (look.droop_deg > 0.f) {
    const float avail = std::acos(clampf(dot(inst.frame.z, V3(0, -1, 0)), -1.f, 1.f));
    if (avail > 1e-4f) lean(inst.frame, V3(0, -1, 0), std::min(1.f, look.droop_deg * DEG / avail));
  }
  hls_shift(inst.tint, pr.random("shift_hue"), pr.random("shift_lum"), pr.random("shift_sat"));
  for (int c = 0; c < 3; ++c) inst.tint[c] *= look.tint[c];
  const float tint4[4] = {inst.tint[0], inst.tint[1], inst.tint[2], inst.tint[3]};

  // the sheet: picture (u, v) -> the leaf's local frame. Along the axis the
  // tangent turns by the bend and the gravitropism; the section twists,
  // folds at the rib and slides by the lateral profile.
  const V3 down_local = inst.frame.local(inst.frame.o + V3(0, -1, 0));
  auto local = [&](float u, float v) -> V3 {
    const float pv = (v - vmin) / vspan, pu = (u - umin) / uspan;
    const float s = pv * L;
    const float ang = bend * pv;
    // the axis point: an arc of `bend` about x, plus the sideways curve
    V3 ap;
    if (std::fabs(bend) > 1e-4f) { const float R = L / bend; ap = V3(0.f, R * (1.f - std::cos(ang)), R * std::sin(ang)); }
    else ap = V3(0.f, 0.f, s);
    ap.x += axis_curve.eval(pv) * L;
    // gravitropism pulls the far part toward the ground
    ap += down_local * (std::sin(std::min(PI * 0.5f, std::fabs(gravi))) * (gravi >= 0.f ? 1.f : -1.f) * pv * pv * L);
    V3 t(0.f, std::sin(ang), std::cos(ang));
    // the section basis, twisted about the tangent
    V3 sx = normalize(cross(V3(0, 1, 0), t), V3(1, 0, 0));
    V3 sy = cross(t, sx);
    const float tw = twist * pv;
    if (tw != 0.f) { const V3 nx = sx * std::cos(tw) + sy * std::sin(tw); sy = sy * std::cos(tw) - sx * std::sin(tw); sx = nx; }
    const float across = (pu - 0.5f) * W + lateral.eval(pu - 0.5f) * W;
    const float fold = rib * rib_curve.eval(pv) * 0.5f;
    const float lift = std::fabs(across) * std::tan(fold);
    V3 p = ap + sx * across + sy * lift;
    return V3(p.x * axis_scale.x, p.y * axis_scale.y, p.z * axis_scale.z);
  };

  // wind: the flexibility curve accumulated along the axis
  const float breeze = pr.random("breeze_strength", 0.f, 0, 1.f);
  const Curve &flexc = pr.curve("breeze_flexibility");
  float flex_cdf[17];
  {
    float acc = 0.f;
    flex_cdf[0] = 0.f;
    for (int i = 1; i <= 16; ++i) { acc += std::max(0.f, flexc.eval(((float)i - 0.5f) / 16.f)); flex_cdf[i] = acc; }
    for (int i = 0; i <= 16; ++i) flex_cdf[i] = acc > 1e-6f ? flex_cdf[i] / acc : (float)i / 16.f;
  }
  const float phase = inst.wind_phase, bendw = inst.wind_bend;
  auto wind_at = [&](float pv, float w4[4]) {
    const float x = clampf(pv, 0.f, 1.f) * 16.f;
    const int i = std::min(15, (int)x);
    w4[0] = phase; w4[1] = bendw;
    // in metres, like every flutter weight: a third of the leaf's length
    w4[2] = breeze * (flex_cdf[i] + (flex_cdf[i + 1] - flex_cdf[i]) * (x - (float)i)) * L * 0.35f;
    w4[3] = -1.f;
  };

  // the grid over the picture: 2^boost cells per grid_x/grid_y of the picture
  int bx = pr.i("grid_boost_x"), by = pr.i("grid_boost_y");
  if (pr.b("lod_affects_grid", true)) { bx -= ctx.lod; by -= ctx.lod; }
  bx = std::max(-8, std::min(8, bx + (int)std::floor(ctx.detail)));
  by = std::max(-8, std::min(8, by + (int)std::floor(ctx.detail)));
  const float cell_u = std::max(0.02f, pr.f("grid_x", 0.25f)) / std::ldexp(1.f, bx);
  const float cell_v = std::max(0.02f, pr.f("grid_y", 0.25f)) / std::ldexp(1.f, by);
  const int nu = std::max(1, std::min(256, (int)std::ceil(uspan / cell_u)));
  const int nv = std::max(1, std::min(256, (int)std::ceil(vspan / cell_v)));

  for (int k = 0; k < planes; ++k) {
    Frame f = inst.frame;
    if (k > 0) f = f.rotated(f.z, PI / (float)planes * (float)k);
    auto emit = [&](float u, float v) -> uint32_t {
      const float e = 2e-3f;
      const V3 c = local(u, v);
      const V3 du = local(u + e, v) - local(u - e, v);
      const V3 dv = local(u, v + e) - local(u, v - e);
      V3 nn = normalize(f.dir(cross(dv, du)), f.y);
      if (!smooth_rib && rib != 0.f && std::fabs(u - 0.5f) < 1e-4f) nn = f.y; // a crease keeps its flat normal
      float w4[4];
      wind_at((v - vmin) / vspan, w4);
      return out.vertex(f.world(c), nn, u, v, w4, tint4);
    };
    out.begin(inst, PlantPartKind::Leaf, ctx.material_for(n, plane_mat[(size_t)k]), true, "cutout leaf");
    for (int j = 0; j < nv; ++j)
      for (int i = 0; i < nu; ++i) {
        const float x0 = umin + uspan * (float)i / (float)nu, x1 = umin + uspan * (float)(i + 1) / (float)nu;
        const float y0 = vmin + vspan * (float)j / (float)nv, y1 = vmin + vspan * (float)(j + 1) / (float)nv;
        const bool c00 = inside(outline, x0, y0), c10 = inside(outline, x1, y0), c11 = inside(outline, x1, y1), c01 = inside(outline, x0, y1);
        if (c00 && c10 && c11 && c01) {
          const uint32_t a = emit(x0, y0), b = emit(x1, y0), c = emit(x1, y1), d = emit(x0, y1);
          out.quad(a, b, c, d);
          continue;
        }
        std::vector<P2> piece = clip_cell(outline, x0, y0, x1, y1);
        if (piece.size() < 3 || std::fabs(signed_area(piece)) < 1e-9f) continue;
        std::vector<int> tris;
        ear_clip(piece, tris);
        std::vector<uint32_t> ids(piece.size());
        for (size_t q = 0; q < piece.size(); ++q) ids[q] = emit(piece[q].x, piece[q].y);
        for (size_t q = 0; q + 2 < tris.size(); q += 3) out.tri(ids[(size_t)tris[q]], ids[(size_t)tris[q + 1]], ids[(size_t)tris[q + 2]]);
      }
    out.end();
  }

  sk.tip.frame = inst.frame;
  sk.tip.frame.o = inst.frame.world(local(0.5f * (umin + umax), vmax));
  sk.tip.primal = 1.f;
  sk.tip.dist = inst.dist_root + L;
  sk.has_tip = true;
  sk.bottom.frame = inst.frame;
  sk.has_bottom = true;
}

} // namespace plant
} // namespace gpx
