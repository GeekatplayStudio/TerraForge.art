// Geekatplay TerraForge - FieldStones: a field of stones, sized in metres,
// evaluated wherever it is asked rather than stored.
//
// The raster FakeStones node lays stones into a heightmap, so its smallest
// stone is a couple of texels - about 14 m on a 5 km tile, and at most 25
// of them to the hectare. This one is a function: the GPU calls it per
// vertex while the tessellator subdivides, and per pixel for the shading
// normal, so a 30 cm cobble is as expressible as a 3 m boulder and there is
// no ceiling on how many there are. Terragen reaches the same place from
// the same argument (tg2 guide p4: procedurals "can be computed accurately
// at virtually any scale... theoretically 'infinite' detail", at the cost
// of being computed at render time).
//
// The maths lives in gpx/stones.hpp, mirrored into GLSL as `gpxf_stones`;
// this file is the node and its emitter, which is one call each.
#include "../field_glsl_internal.hpp"
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"
#include "gpx/stones.hpp"
#include <algorithm>

namespace gpx {

namespace {

// The tile's width in metres, so an author sizes stones in metres and the
// field stays free of any resolution. The studio keeps `size_m` in step
// with the project (studio/scene_nodes_objects.cpp).
float tile_m(const Node &n) {
  return std::max(n.attrs.get_f("size_m", 5000.f), 1e-3f);
}

stones::Params read(const Node &n) {
  stones::Params p;
  // one stone's worth of ground: the cell is the stone's own diameter, so
  // density is the only thing that decides how packed a field is
  p.cell = std::max(n.attrs.get_f("stone_m", 0.35f), 1e-4f) / tile_m(n);
  p.density = n.attrs.get_f("density", 0.7f);
  p.tallness = n.attrs.get_f("tallness", 0.6f);
  p.flatten = n.attrs.get_f("flatten", 0.25f);
  p.bury = n.attrs.get_f("bury", 0.25f);
  p.tilt = n.attrs.get_f("tilt", 0.35f);
  p.spread = n.attrs.get_f("spread", 0.7f);
  p.elongation = n.attrs.get_f("elongation", 0.5f);
  p.rough = n.attrs.get_f("rough", 0.45f);
  p.facet = n.attrs.get_f("facet", 0.55f);
  p.bumpy = n.attrs.get_f("bumpy", 0.4f);
  p.cluster = n.attrs.get_f("cluster", 0.5f);
  // a drift is given in metres and held in cells, because the cell is the
  // stone: "drifts about 5 m across" survives a change of stone size
  p.cluster_cells = std::max(n.attrs.get_f("cluster_m", 5.f) /
                                 std::max(n.attrs.get_f("stone_m", 0.35f), 1e-3f),
                             1.f);
  p.seed = n.attrs.get_seed("seed");
  return p;
}

// The octave budget: every octave halves the stone and doubles the count,
// so the far ground pays for boulders alone and the ground underfoot pays
// for the gravel too. The caller's budget is the field domain's LOD.
int octaves_for(const Node &n, float lod) {
  const int want = n.attrs.get_i("octaves", 3);
  return std::clamp((int)(lod) - 3, 1, std::clamp(want, 1, stones::MAX_OCTAVES));
}

} // namespace

REGISTER_NODE(
    FieldStones, "Field Noise",
    "A field of stones - boulders, cobbles and gravel - as a function, at any scale",
    [](Node &n) {
      n.add_field_in("position", FieldType::Vector, true);
      add_float(n.attrs, "stone_m", "Largest stone (m)", 0.35f, 0.005f, 200.f, "Stones", true)
          .tooltip = "The biggest stone's width across, in metres, and it is\n"
                     "really metres: 0.35 is a stone you could pick up, 0.05\n"
                     "is gravel, 3 is a boulder. Every size below it comes\n"
                     "from the octaves, each half as wide and four times as\n"
                     "many, so one field holds boulders, cobbles and grit at\n"
                     "once.";
      add_int(n.attrs, "octaves", "Sizes", 3, 1, 5, "Stones")
          .tooltip = "How many halvings of the stone size to add. 1 is one\n"
                     "size of stone; 4 reaches gravel a sixteenth as wide.\n"
                     "Distance takes octaves away again, so this is a\n"
                     "ceiling, not a cost.";
      add_float(n.attrs, "density", "Density", 0.7f, 0.f, 1.f, "Stones")
          .tooltip = "The share of the ground that holds a stone.";
      add_float(n.attrs, "tallness", "Tallness", 0.6f, 0.05f, 2.f, "Stones")
          .tooltip = "A stone's height as a fraction of its radius.";
      add_float(n.attrs, "spread", "Size spread", 0.7f, 0.f, 1.f, "Shape")
          .tooltip = "0: every stone the same size. 1: the power-law\n"
                     "spectrum a scree slope has - many small, a few large.";
      add_float(n.attrs, "flatten", "Flatten", 0.25f, 0.f, 1.f, "Shape")
          .tooltip = "Raises the top into a plateau while keeping the\n"
                     "footprint: 0 boulders, 1 slabs.";
      add_float(n.attrs, "bury", "Settled into the ground", 0.25f, 0.f, 0.9f, "Shape")
          .tooltip = "How deep a stone sits. Buried stones show only their\n"
                     "tops, and the ground cuts their outline instead of\n"
                     "meeting them tangentially.";
      add_float(n.attrs, "elongation", "Elongation", 0.5f, 0.f, 1.f, "Shape")
          .tooltip = "How much longer a stone may be one way than the other,\n"
                     "turned as it fell. Round in plan is the tell of a\n"
                     "procedural field.";
      add_float(n.attrs, "rough", "Outline roughness", 0.45f, 0.f, 1.f, "Shape")
          .tooltip = "How far the outline departs from an ellipse.";
      add_float(n.attrs, "facet", "Broken faces", 0.55f, 0.f, 1.f, "Shape")
          .tooltip = "Cuts flat faces into each stone. 0 leaves rounded\n"
                     "pebbles; high values give the angular, broken look of\n"
                     "quarried or frost-shattered rock.";
      add_float(n.attrs, "bumpy", "Surface relief", 0.4f, 0.f, 1.f, "Shape")
          .tooltip = "How far a stone's own surface departs from a smooth\n"
                     "shell. 0 is polished.";
      add_float(n.attrs, "tilt", "Lean", 0.35f, 0.f, 1.f, "Shape")
          .tooltip = "Moves each stone's high point off centre, so it has a\n"
                     "downhill side rather than being a dome.";
      add_float(n.attrs, "cluster", "Clustering", 0.5f, 0.f, 1.f, "Stones")
          .tooltip = "Stones are not spread evenly: they collect in drifts\n"
                     "with bare ground between. 0 scatters them uniformly,\n"
                     "1 gathers them hard. The count is unchanged either\n"
                     "way - this rearranges a field, it does not thin it.";
      add_float(n.attrs, "cluster_m", "Drift size (m)", 5.f, 0.05f, 2000.f, "Stones", true)
          .tooltip = "How far across one drift of stones is, in metres.";
      add_seed(n.attrs, "seed", "Seed", 0, "Stones");
      add_float(n.attrs, "size_m", "Terrain size (m)", 5000.f, 1.f, 1000000.f, "Stones", true)
          .tooltip = "The tile's width; the studio keeps this in step with\n"
                     "the project so the size above means metres.";

      n.add_field_out("out", FieldType::Number,
                      [](const Node &self, const FieldContext &ctx) {
                        float p[3];
                        self.in_field("position", ctx,
                                      FieldValue::vector(ctx.pos[0], ctx.pos[1], ctx.pos[2]))
                            .as_vector(p);
                        float h = 0.f, m = 0.f;
                        stones::field(read(self), p[0], p[2],
                                      octaves_for(self, ctx.lod), h, m);
                        return FieldValue(h);
                      });
      // the stones alone, as a mask: what shades them differently from the
      // ground they lie on, which sells a stone field as much as its relief
      n.add_field_out("mask", FieldType::Number,
                      [](const Node &self, const FieldContext &ctx) {
                        float p[3];
                        self.in_field("position", ctx,
                                      FieldValue::vector(ctx.pos[0], ctx.pos[1], ctx.pos[2]))
                            .as_vector(p);
                        float h = 0.f, m = 0.f;
                        stones::field(read(self), p[0], p[2],
                                      octaves_for(self, ctx.lod), h, m);
                        return FieldValue(m);
                      });
    },
    [](Node &) {})

// --------------------------------------------------------------- the mirror
namespace {

// Both outputs are the same call; only the component differs.
std::string emit_stones(const Node &n, const glslgen::InputFn &in,
                        glslgen::EmitCtx &ctx, int component) {
  using glslgen::f2s;
  const std::string p = in("#position", ctx.pos.c_str());
  const stones::Params sp = read(n);
  const int want = std::clamp(n.attrs.get_i("octaves", 3), 1, stones::MAX_OCTAVES);
  // the octave budget, from the caller's LOD, exactly as the CPU does it
  const std::string oct = ctx.declare(
      "int", "stoct",
      "clamp(int(" + ctx.lod + ") - 3, 1, " + std::to_string(want) + ")");
  const std::string v = ctx.declare(
      "vec2", "stones",
      "gpxf_stones(vec2((" + p + ").x, (" + p + ").z), " + f2s(sp.cell) + ", " +
          f2s(sp.density) + ", " + f2s(sp.tallness) + ", " + f2s(sp.flatten) +
          ", " + f2s(sp.bury) + ", " + f2s(sp.tilt) + ", " + f2s(sp.spread) +
          ", " + f2s(sp.elongation) + ", " + f2s(sp.rough) + ", " +
          f2s(sp.facet) + ", " + f2s(sp.bumpy) + ", " + f2s(sp.cluster) +
          ", " + f2s(sp.cluster_cells) + ", " +
          std::to_string((unsigned)sp.seed) + "u, " + oct + ")");
  return "vec4(" + v + (component == 0 ? ".x" : ".y") + ", 0.0, 0.0, 1.0)";
}

struct StonesEmitterRegistrar {
  StonesEmitterRegistrar() {
    glslgen::reg("FieldStones", [](const Node &n, const glslgen::InputFn &in,
                                   glslgen::EmitCtx &ctx) {
      return emit_stones(n, in, ctx, 0);
    });
    glslgen::reg_out("FieldStones", "mask",
                     [](const Node &n, const glslgen::InputFn &in,
                        glslgen::EmitCtx &ctx) {
                       return emit_stones(n, in, ctx, 1);
                     });
  }
};
const StonesEmitterRegistrar reg_stones_emitter;

} // namespace

} // namespace gpx
