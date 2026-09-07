// Geekatplay TerraForge - FieldGrass: a sward of grass, sized in metres,
// evaluated wherever it is asked rather than stored.
//
// The stones' argument, one scale down. A heightmap on a 5 km tile has
// texels metres across; a tuft of grass is centimetres, so a raster node can
// no more hold grass than it could hold a pebble. This is a function of a
// point: the GPU calls it per vertex while the tessellator subdivides and
// per pixel for the shading normal, so the sward keeps resolving as the
// camera comes down to it.
//
// The maths lives in gpx/grass.hpp, mirrored into GLSL as `gpxf_grass`;
// this file is the node and its emitters, which is one call each.
#include "../field_glsl_internal.hpp"
#include "gpx/grass.hpp"
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"
#include <algorithm>
#include <cmath>

namespace gpx {

namespace {

float tile_m(const Node &n) {
  return std::max(n.attrs.get_f("size_m", 5000.f), 1e-3f);
}

grass::Params read(const Node &n) {
  grass::Params p;
  const float tuft_m = std::max(n.attrs.get_f("tuft_m", 0.12f), 1e-4f);
  p.cell = tuft_m / tile_m(n);
  // The blade height is authored in metres and held as a multiple of the
  // tuft's radius, because that is what the profile actually scales by. A
  // tuft of radius rad*cell reaches rad*cell*height, so for the tallest
  // blade to stand `blade_m` above the ground the multiple is 2*blade/tuft.
  p.height = 2.f * std::max(n.attrs.get_f("blade_m", 0.16f), 1e-5f) / tuft_m;
  p.density = n.attrs.get_f("density", 0.85f);
  p.sharp = n.attrs.get_f("sharp", 0.6f);
  p.blade = n.attrs.get_f("blade", 0.7f);
  p.fineness = n.attrs.get_f("fineness", 0.5f);
  p.wind = n.attrs.get_f("wind", 0.35f);
  // One trig call per evaluation of the node, not per tuft: the inner loop
  // never sees an angle.
  const float a = n.attrs.get_f("wind_deg", 0.f) * 0.017453292519943295f;
  p.wind_x = std::cos(a);
  p.wind_z = std::sin(a);
  p.bend = n.attrs.get_f("bend", 0.5f);
  p.spread = n.attrs.get_f("spread", 0.55f);
  p.variation = n.attrs.get_f("variation", 0.6f);
  p.height_var = n.attrs.get_f("height_var", 0.5f);
  p.size_step = std::pow(2.f, -n.attrs.get_f("size_mix", 0.f));
  p.cluster = n.attrs.get_f("cluster", 0.3f);
  p.cluster_cells =
      std::max(n.attrs.get_f("cluster_m", 1.2f) / tuft_m, 1.f);
  p.bare = n.attrs.get_f("bare", 0.2f);
  p.bare_cells = std::max(n.attrs.get_f("bare_m", 7.f) / tuft_m, 1.f);
  p.seed = n.attrs.get_seed("seed");
  return p;
}

int octaves_for(const Node &n, float lod) {
  const int want = n.attrs.get_i("octaves", 3);
  return std::clamp((int)(lod) - 3, 1, std::clamp(want, 1, grass::MAX_OCTAVES));
}

} // namespace

REGISTER_NODE(
    FieldGrass, "Field Noise",
    "A sward of grass - tufts, blades and bare ground - as a function, at any scale",
    [](Node &n) {
      n.add_field_in("position", FieldType::Vector, true);

      add_float(n.attrs, "tuft_m", "Tuft size (m)", 0.12f, 0.004f, 20.f,
                "Grass", true)
          .tooltip = "How far across one tuft of grass is, in metres. 0.12 is\n"
                     "a clump of meadow grass, 0.03 is a lawn, 1 is tussock.\n"
                     "\n"
                     "Below about a centimetre on a 5 km tile the tile's own\n"
                     "coordinates run out of precision and the tufts go\n"
                     "blocky. Shrink the terrain, not the grass.";
      add_float(n.attrs, "blade_m", "Blade height (m)", 0.16f, 0.002f, 20.f,
                "Grass", true)
          .tooltip = "How tall the longest blades stand, in metres. Grass is\n"
                     "taller than it is wide, which is most of what tells it\n"
                     "apart from a field of small stones.";
      add_int(n.attrs, "octaves", "Sizes", 3, 1, 5, "Grass")
          .tooltip = "How many halvings of the tuft size to add, so how wide\n"
                     "a range of sizes one sward holds - big clumps with\n"
                     "finer grass filling between them.";
      add_float(n.attrs, "density", "Amount", 0.85f, 0.f, 1.f, "Grass")
          .tooltip = "How much grass there is. Up to about three quarters it\n"
                     "thins the sward; past that every cell holds a tuft and\n"
                     "they grow into one another, so 1 closes it completely\n"
                     "with no ground showing through.";
      add_seed(n.attrs, "seed", "Seed", 0, "Grass");

      add_float(n.attrs, "sharp", "Pointedness", 0.6f, 0.f, 1.f, "Blades")
          .tooltip = "How sharply a tuft comes to a point. This is the one\n"
                     "control that most decides whether the field reads as\n"
                     "grass or as gravel: 0 gives domes, which is what a\n"
                     "stone is, and no amount of blade detail rescues that.";
      add_float(n.attrs, "blade", "Blade relief", 0.7f, 0.f, 1.f, "Blades")
          .tooltip = "How strongly the individual blades show against the\n"
                     "tuft they belong to. 0 is a smooth mound.";
      add_float(n.attrs, "fineness", "Blade count", 0.5f, 0.f, 1.f, "Blades")
          .tooltip = "0 a few broad blades, 1 many fine ones.";
      add_float(n.attrs, "spread", "Size variation", 0.55f, 0.f, 1.f, "Blades")
          .tooltip = "0: every tuft the same size. 1: many small tufts and a\n"
                     "few large, which is what a real sward has.";
      add_float(n.attrs, "variation", "Shape variation", 0.6f, 0.f, 1.f,
                "Blades")
          .tooltip = "How much tufts differ from one another. 0 shapes every\n"
                     "tuft in the field alike, which is the look of a\n"
                     "texture; 1 puts fine soft grass and coarse spiky\n"
                     "clumps side by side.";

      add_float(n.attrs, "height_var", "Height variation", 0.5f, 0.f, 1.f,
                "Blades")
          .tooltip = "How much tufts differ in height from one another,\n"
                     "about the average. The average is unchanged\n"
                     "whatever this is set to.";
      add_float(n.attrs, "size_mix", "Size mix", 0.f, -1.f, 1.f, "Blades")
          .tooltip = "Which sizes the field is actually made of, across the\n"
                     "octaves it has. Below zero leans toward the large and\n"
                     "the small become an accent; above zero the small take\n"
                     "over and the large are the accent. 0 gives every size\n"
                     "the same share, which is what it always did.";
      add_float(n.attrs, "wind", "Wind", 0.35f, 0.f, 1.f, "Wind")
          .tooltip = "How far the blades lean. The whole field leans one\n"
                     "way, which is the strongest single cue that what you\n"
                     "are looking at is grass and not small stones - those\n"
                     "each lean whichever way they fell.";
      add_float(n.attrs, "wind_deg", "Wind direction", 0.f, -180.f, 180.f,
                "Wind")
          .tooltip = "Which way the wind is blowing across the ground.";
      add_float(n.attrs, "bend", "Tall blades bend more", 0.5f, 0.f, 1.f,
                "Wind")
          .tooltip = "How much further the tall blades lean than the short\n"
                     "ones. They do, so this is 0.5 rather than 0.";

      add_float(n.attrs, "cluster", "Cluster / repel", 0.3f, -1.f, 1.f,
                "Ground")
          .tooltip = "Above zero the tufts gather into patches and are\n"
                     "pulled together inside one; below zero they stand off\n"
                     "from one another. The count is unchanged either way.";
      add_float(n.attrs, "cluster_m", "Patch size (m)", 1.2f, 0.02f, 500.f,
                "Ground", true)
          .tooltip = "How far across one patch of tufts is, in metres.";
      add_float(n.attrs, "bare", "Bare ground", 0.2f, 0.f, 1.f, "Ground")
          .tooltip = "How much ground is bare of grass altogether. Grass is\n"
                     "not a carpet - it gives out where it is trodden, dry\n"
                     "or shaded - and a sward that only ever thins a little\n"
                     "reads as one.";
      add_float(n.attrs, "bare_m", "Bare patch size (m)", 7.f, 0.05f, 2000.f,
                "Ground", true)
          .tooltip = "How far across a bare patch is, in metres.";

      add_float(n.attrs, "size_m", "Terrain size (m)", 5000.f, 1.f, 1000000.f,
                "Grass", true)
          .tooltip = "The tile's width; the studio keeps this in step with\n"
                     "the project so the sizes above mean metres.";

      n.add_field_out("out", FieldType::Number,
                      [](const Node &self, const FieldContext &ctx) {
                        float p[3];
                        self.in_field("position", ctx,
                                      FieldValue::vector(ctx.pos[0], ctx.pos[1], ctx.pos[2]))
                            .as_vector(p);
                        float h = 0.f, m = 0.f, s = 0.f;
                        grass::field(read(self), p[0], p[2],
                                     octaves_for(self, ctx.lod), h, m, s);
                        return FieldValue(h);
                      });
      n.add_field_out("mask", FieldType::Number,
                      [](const Node &self, const FieldContext &ctx) {
                        float p[3];
                        self.in_field("position", ctx,
                                      FieldValue::vector(ctx.pos[0], ctx.pos[1], ctx.pos[2]))
                            .as_vector(p);
                        float h = 0.f, m = 0.f, s = 0.f;
                        grass::field(read(self), p[0], p[2],
                                     octaves_for(self, ctx.lod), h, m, s);
                        return FieldValue(m);
                      });
      // which tuft this is, so a material can colour each one differently -
      // grass runs from dry to green within a stride, and one colour over a
      // whole sward is what gives a procedural one away
      n.add_field_out("shade", FieldType::Number,
                      [](const Node &self, const FieldContext &ctx) {
                        float p[3];
                        self.in_field("position", ctx,
                                      FieldValue::vector(ctx.pos[0], ctx.pos[1], ctx.pos[2]))
                            .as_vector(p);
                        float h = 0.f, m = 0.f, s = 0.f;
                        grass::field(read(self), p[0], p[2],
                                     octaves_for(self, ctx.lod), h, m, s);
                        return FieldValue(s);
                      });
    },
    [](Node &) {})

// --------------------------------------------------------------- the mirror
namespace {

std::string emit_grass(const Node &n, const glslgen::InputFn &in,
                       glslgen::EmitCtx &ctx, int component) {
  using glslgen::f2s;
  const std::string p = in("#position", ctx.pos.c_str());
  const grass::Params gp = read(n);
  const int want = std::clamp(n.attrs.get_i("octaves", 3), 1, grass::MAX_OCTAVES);
  const std::string oct = ctx.declare(
      "int", "gsoct",
      "clamp(int(" + ctx.lod + ") - 3, 1, " + std::to_string(want) + ")");
  const std::string v = ctx.declare(
      "vec3", "grass",
      "gpxf_grass(vec2((" + p + ").x, (" + p + ").z), " + f2s(gp.cell) + ", " +
          f2s(gp.density) + ", " + f2s(gp.height) + ", " + f2s(gp.sharp) + ", " +
          f2s(gp.blade) + ", " + f2s(gp.fineness) + ", " + f2s(gp.wind) + ", " +
          f2s(gp.wind_x) + ", " + f2s(gp.wind_z) + ", " + f2s(gp.bend) + ", " +
          f2s(gp.spread) + ", " + f2s(gp.variation) + ", " +
          f2s(gp.height_var) + ", " + f2s(gp.size_step) + ", " +
          f2s(gp.cluster) +
          ", " + f2s(gp.cluster_cells) + ", " + f2s(gp.bare) + ", " +
          f2s(gp.bare_cells) + ", " + std::to_string((unsigned)gp.seed) +
          "u, " + oct + ")");
  const char *lane = component == 0 ? ".x" : (component == 1 ? ".y" : ".z");
  return "vec4(" + v + lane + ", 0.0, 0.0, 1.0)";
}

struct GrassEmitterRegistrar {
  GrassEmitterRegistrar() {
    glslgen::reg("FieldGrass", [](const Node &n, const glslgen::InputFn &in,
                                  glslgen::EmitCtx &ctx) {
      return emit_grass(n, in, ctx, 0);
    });
    glslgen::reg_out("FieldGrass", "mask",
                     [](const Node &n, const glslgen::InputFn &in,
                        glslgen::EmitCtx &ctx) {
                       return emit_grass(n, in, ctx, 1);
                     });
    glslgen::reg_out("FieldGrass", "shade",
                     [](const Node &n, const glslgen::InputFn &in,
                        glslgen::EmitCtx &ctx) {
                       return emit_grass(n, in, ctx, 2);
                     });
  }
};
const GrassEmitterRegistrar reg_grass_emitter;

} // namespace

} // namespace gpx
