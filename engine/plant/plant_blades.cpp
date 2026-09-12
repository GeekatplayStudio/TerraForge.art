// Geekatplay TerraForge - blades on a segment (plant_internal.hpp:
// segment_blades).
//
// A blade is a sheet that runs along a segment rather than hanging off it:
// the needles of a conifer twig, the leaflets of a frond, the flat of a
// grass leaf, the barbs of a feather. The manual gives them to the Segment
// node rather than making them children because they are cheap - no
// instance, no attachment, no children of their own - and a plant needs
// thousands of them.
//
// The shape is two curves and a width: the profile says how wide the blade
// is along the segment (so it can taper to a point), the section says how
// its cross-line rises and falls (so it can be a gutter or a fold rather
// than a flat strip), and pinching decides whether the profile cuts the
// section off or shrinks it. `number` blades are spread over `spread`
// degrees round the axis; Symmetrical mirrors each one across the axis,
// Full width runs one sheet right through it, and Simple flat is the cheap
// two-triangle strip a distant tree only ever needs.
//
// They flutter rather than bend: the wind weight grows with the distance
// from the axis, so the wood stays put and the foliage moves.
#include "plant/plant_internal.hpp"
#include <algorithm>

namespace gpx {
namespace plant {

void segment_blades(BuildCtx &ctx, const Node &n, Instance &inst, MeshOut &out, const SegmentShape &sh) {
  const ParamReader pr(ctx, n, inst);
  const int number = pr.i("blade_number");
  if (number <= 0 || sh.rings.size() < 2) return;

  const int style = pr.choice("blade_style", 1);
  const float start = clampf(pr.random("blade_start"), 0.f, 1.f);
  const float end = clampf(pr.random("blade_end", 0.f, 0, 1.f), 0.f, 1.f);
  if (end <= start) return;
  const float spread = pr.random("blade_spread", 0.f, 0, 180.f) * DEG;
  const float spread_offset = pr.random("blade_spread_offset") * DEG;
  float width = pr.random("blade_width", 0.f, 0, 0.05f) * inst.scale;
  if (pr.b("blade_autosize")) width *= std::max(sh.length, 0.01f);
  if (width <= 1e-5f) return;
  const bool from_axis = pr.b("blade_from_axis", true);
  const bool follow_axis = pr.b("blade_follow_axis", true);
  const Curve profile = pr.curve("blade_profile");
  const Curve section = pr.curve("blade_section");
  const float section_h = pr.random("blade_section_height", 0.f, 0, 0.2f);
  const float pinching = clampf(pr.random("blade_pinching"), 0.f, 1.f);
  const int across = std::max(2, subdiv(ctx, (float)pr.i("blade_subdiv", 3), 2));
  const int dist_mode = pr.choice("blade_dist_mode", 1);
  const int uv_mode = pr.choice("blade_uv_mode", 2);
  const bool uv_extension = pr.b("blade_uv_extension");
  const bool v_from_end = pr.b("blade_map_v_from_end");
  const float u_tile = pr.f("blade_u_tile", 1.f), v_tile = pr.f("blade_v_tile", 1.f);
  const float u_offset = pr.f("blade_u_offset"), v_offset = pr.f("blade_v_offset");
  const float disp = pr.random("blade_disp_amount");
  const float disp_radial = pr.random("blade_disp_radial");

  // which of the blade materials each blade wears
  const std::vector<const Node *> mats = ctx.materials_of(n);
  const bool have_blade_mat = mats.size() > 5 && (mats[5] || (mats.size() > 6 && mats[6]));
  auto material_for_blade = [&](int blade) {
    if (!have_blade_mat) return ctx.material_for(n, 5);
    const bool second = mats.size() > 6 && mats[6] != nullptr;
    if (!second) return ctx.material_for(n, 5);
    switch (dist_mode) {
      case 0: return ctx.material_for(n, 5 + (int)(hash_unit(hash_u32(inst.id, 7u)) * 2.f) % 2);
      case 1: return ctx.material_for(n, 5 + (int)(hash_unit(hash_u32(inst.id, (uint32_t)blade)) * 2.f) % 2);
      default: return ctx.material_for(n, 5 + (blade % 2));
    }
  };

  // the samples of the axis the blades run along
  const size_t first = 0, last = sh.rings.size() - 1;
  const float wind_flutter = pr.random("blade_flexibility", 0.f, 0, 1.f);

  for (int b = 0; b < number; ++b) {
    const float t = number > 1 ? (float)b / (float)(number - 1) : 0.5f;
    const float az = spread_offset + (number > 1 ? spread * (t - 0.5f) : 0.f);
    const int sides = style == 1 ? 2 : 1; // Symmetrical draws both sides
    out.begin(inst, PlantPartKind::Blade, material_for_blade(b), true, "blade");
    for (int side = 0; side < sides; ++side) {
      const float sign = side == 0 ? 1.f : -1.f;
      const int cols = style == 2 ? across * 2 : across; // Full width runs through the axis
      std::vector<uint32_t> prev, cur;
      for (size_t i = first; i <= last; ++i) {
        const Instance::AxisSample &s = sh.rings[i];
        if (s.primal < start || s.primal > end) {
          prev.clear();
          continue;
        }
        const float along = clampf((s.primal - start) / std::max(end - start, 1e-5f), 0.f, 1.f);
        const float w = width * std::max(profile.eval(along), 0.f);
        if (w <= 1e-6f) {
          prev.clear();
          continue;
        }
        const V3 tangent = follow_axis ? s.t : inst.frame.z;
        const V3 nx = s.n, ny = normalize(cross(tangent, nx), perpendicular(tangent));
        const V3 dir = normalize(nx * std::cos(az) + ny * std::sin(az), nx) * sign;
        const V3 up = normalize(cross(dir, tangent), ny);
        const V3 root = from_axis ? s.p : s.p + dir * s.radius;
        const float wind4_bend = wind_bend_at(ctx, inst, s.primal, sh.wind_flex);
        cur.clear();
        for (int j = 0; j <= cols; ++j) {
          float u = (float)j / (float)cols;
          float x = u;                       // 0 at the axis, 1 at the blade's edge
          if (style == 2) x = u * 2.f - 1.f; // through the axis: -1 .. 1
          const float ax = std::fabs(x);
          const float rise = section.eval(ax) * section_h * w * (pinching > 0.f ? (1.f - pinching * ax) : 1.f);
          V3 p = root + dir * (x * w) + up * rise;
          if (disp != 0.f || disp_radial != 0.f)
            p += up * (disp * 0.02f * w) + dir * (disp_radial * 0.02f * w * ax);
          const V3 nrm = normalize(cross(dir, tangent), up);
          // in metres: a blade's tip whips by about a third of its length
          const float flutter = wind_flutter * ax * sh.length * 0.35f;
          const float wind4[4] = {sh.wind_phase + 0.13f * (float)b, wind4_bend, flutter, -1.f};
          float uu = uv_mode == 0 ? x * u_tile : (style == 2 ? (x * 0.5f + 0.5f) : x) * u_tile;
          if (uv_extension && style == 1) uu = (side == 0 ? 0.5f + 0.5f * x : 0.5f - 0.5f * x) * u_tile;
          float vv = (uv_mode == 2 ? along : s.dist) * v_tile;
          if (v_from_end) vv = -vv;
          cur.push_back(out.vertex(p, nrm, uu + u_offset, vv + v_offset, wind4, sh.tint));
        }
        if (!prev.empty())
          for (int j = 0; j < cols; ++j)
            out.quad(prev[(size_t)j], prev[(size_t)j + 1], cur[(size_t)j + 1], cur[(size_t)j]);
        prev = cur;
      }
      if (style == 3) break; // Simple flat: one strip, both sides of one sheet
    }
    out.end();
  }
  ctx.mesh->leaves += number;
}

} // namespace plant
} // namespace gpx
